// =============================================================================
//  Efeitos de distorção (Fase 7.3 §24, §27, §28, §36, §40).
//
//  Shake · Turbulência · Onda · Lente · Ondulação que dissolve
//
//  Todos mexem na GEOMETRIA: nenhum deles cabe no passe de cor fundido, e
//  todos declaram a margem que leem para o grafo recortar a região sem cortar
//  o que eles precisam.
//
//  Shake usa uma trajetória contínua em segundos e reamostragem com bordas
//  configuráveis. Preview, scrubbing e export avaliam a mesma trajetória.
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/effects/ShakeMotion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

// Tetos da faixa DIGITADA do Shake (o slider segue 1000 px · 60 Hz · 500 % · 100 %).
constexpr f32 kShakeMaxAmplitude = 20000.0f;   // px
constexpr f32 kShakeMaxFrequency = 240.0f;     // Hz: acima de fps/2 já é tremor quadro a quadro
constexpr f32 kShakeMaxAmount = 20.0f;         // 2000 %
constexpr f32 kShakeMaxShutter = 4.0f;         // 400 % do quadro
constexpr f32 kShakeMaxDecay = 100.0f;         // 1/s: some em ~50 ms

// Continuous camera shake. Shared CPU path supplies inverse transforms to a
// single GPU pass; no frame cache or random generator state is needed.
class Shake final : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_camera_shake_frag, work));
    }
    enum : u32 { kAmplitudeX, kAmplitudeY, kFrequency, kSeed, kSeparate, kRotation, kSmoothing, kMix,
                 kAmount, kZoom, kStyle, kBlur, kPhase, kEdges, kWave, kDirection, kDecay,
                 kFreqX, kFreqY, kFreqRotation, kFreqZoom, kPhaseY, kStillness, kTwitchFreq, kDrift, kCenterBias };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kShake, "Shake", "Distort", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // Faixas digitadas (edição extrema): a trajetória é função pura do
        // tempo e o passe tem no máximo 8 amostras — nada aqui cresce custo ou
        // memória com o valor; os tetos são só de sanidade numérica.
        p.add_float("amplitude_x", "Horizontal", 20, 0, 1000, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0, kShakeMaxAmplitude);
        p.add_float("amplitude_y", "Vertical", 20, 0, 1000, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0, kShakeMaxAmplitude);
        p.add_float("frequency", "Speed", 8, 0, 60, kParamAnimatable, "Hz");
        p.typed_range(0, kShakeMaxFrequency);
        p.add_int("seed", "Seed", 1, 0, 9999);
        p.add_bool("separate_axes", "Independent axes", true);
        p.add_angle("rotation", "Rotation", 0);
        p.add_float("smoothing", "Smoothness", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("mix", "Mix", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("amount", "Amount", 100, 0, 500, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0, kShakeMaxAmount * 100.0f);
        // Zoom: a trajetória prende a escala em ±2 stops; 200 % já alcança o teto.
        p.add_float("zoom", "Zoom", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0, 200);
        static const char* const styles[] = {"Normal", "Twitchy", "Jumpy"};
        p.add_enum("style", "Style", styles, 3, 0);
        // Obturador em fração do quadro: acima de 100 % o rastro cobre vários
        // quadros (as mesmas 8 amostras, custo fixo).
        p.add_float("motion_blur", "Motion blur", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0, kShakeMaxShutter * 100.0f);
        p.add_float("phase", "Phase", 0, -10000, 10000, kParamAnimatable);
        static const char* const edges[] = {"Reflect", "Clamp", "Tile", "Transparent"};
        p.add_enum("edges", "Edges", edges, 4, 0);
        p.add_float("wave", "Wave", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        // Pacote de paridade (anexados no fim: projeto antigo lê o padrão, que
        // não muda nada). Direção gira o par Horizontal/Vertical — com Vertical
        // em 0 o tremor corre só ao longo dela. Decaimento apaga o tremor por
        // exp(-decaimento · t), t em segundos desde o início da camada.
        p.add_angle("direction", "Direção", 0);
        p.add_float("decay", "Decaimento", 0, 0, 10, kParamAnimatable, "1/s");
        p.typed_range(0, kShakeMaxDecay);
        p.add_float("frequency_x", "Frequência horizontal relativa", 1, 0, 12);
        p.add_float("frequency_y", "Frequência vertical relativa", 1, 0, 12);
        p.add_float("frequency_rotation", "Frequência de rotação relativa", 1, 0, 12);
        p.add_float("frequency_zoom", "Frequência de zoom relativa", 1, 0, 12);
        p.add_float("phase_y", "Fase vertical", 0, -10000, 10000);
        p.add_float("stillness", "Imobilidade", 70, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("twitch_frequency", "Frequência das rajadas", 0, 0, 60, kParamAnimatable, "Hz");
        p.add_float("drift", "Deriva", 15, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("center_bias", "Retorno ao centro", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) <= 0 || e.f(kAmount) <= 0 || e.f(kFrequency) <= 0
            || (e.f(kAmplitudeX) == 0 && e.f(kAmplitudeY) == 0 && e.f(kRotation) == 0 && e.f(kZoom) == 0)
            || died_out(e);
    }
    /// O decaimento já apagou o tremor (mesmo no começo do obturador mais
    /// longo): o passe sairia idêntico à entrada.
    static bool died_out(const EffectEval& e) noexcept {
        const f64 decay = static_cast<f64>(finite_or(e.f(kDecay), 0));
        if (!(decay > 0)) return false;
        const f64 fps = e.framesPerSecond > 0 ? e.framesPerSecond : 30.0;
        const f64 earliest = e.time_frames() / fps - kShakeMaxShutter / fps;
        return std::exp(-std::min(decay, static_cast<f64>(kShakeMaxDecay)) * std::max(0.0, earliest)) < 1e-6;
    }
    f32 input_margin(const EffectEval&) const noexcept override { return 0; }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        shake::Settings s;
        s.amplitudeX = finite_or(e.f(kAmplitudeX), 0); s.amplitudeY = finite_or(e.f(kAmplitudeY), 0);
        s.frequency = std::clamp(finite_or(e.f(kFrequency), 8), 0.f, kShakeMaxFrequency);
        s.seed = e.e(kSeed); s.separate = e.b(kSeparate);
        s.rotation = finite_or(e.f(kRotation), 0); s.zoom = finite_or(e.f(kZoom), 0);
        s.smoothness = std::clamp(finite_or(e.f(kSmoothing), 100) / 100.f, 0.f, 1.f);
        s.amount = std::clamp(finite_or(e.f(kAmount), 100) / 100.f, 0.f, kShakeMaxAmount);
        s.style = e.e(kStyle); s.phase = finite_or(e.f(kPhase), 0);
        s.wave = std::clamp(finite_or(e.f(kWave), 0) / 100.f, 0.f, 1.f);
        const auto scalar = [&](u32 i, f32 fallback, f32 low, f32 high) {
            return i < e.count ? std::clamp(finite_or(e.f(i), fallback), low, high) : fallback;
        };
        s.frequencyScale = {scalar(kFreqX, 1, 0, 12), scalar(kFreqY, 1, 0, 12),
                            scalar(kFreqRotation, 1, 0, 12), scalar(kFreqZoom, 1, 0, 12)};
        s.phaseY = scalar(kPhaseY, 0, -10000, 10000);
        s.stillness = scalar(kStillness, 70, 0, 100) / 100.f;
        s.twitchFrequency = scalar(kTwitchFreq, 0, 0, 60);
        s.drift = scalar(kDrift, 15, 0, 100) / 100.f;
        s.centerBias = scalar(kCenterBias, 0, 0, 100) / 100.f;
        const f64 fps = e.framesPerSecond > 0 ? e.framesPerSecond : 30.0;
        const f64 seconds = e.time_frames() / fps;
        const f32 blur = std::clamp(finite_or(e.f(kBlur), 0) / 100.f, 0.f, kShakeMaxShutter);
        const u32 count = blur > .001f ? 8u : 1u;
        struct { Vec4 rows[16]; Vec4 options; Vec4 bounds; } u{};
        const Rect in = input.region;
        const f32 cx = e.placement ? e.placement->layerWidth * .5f : in.x + in.w*.5f;
        const f32 cy = e.placement ? e.placement->layerHeight * .5f : in.y + in.h*.5f;
        const f32 dir = finite_or(e.f(kDirection), 0) * kDeg2Rad;
        const f32 dirC = std::cos(dir), dirS = std::sin(dir);
        const f64 decay = std::clamp(static_cast<f64>(finite_or(e.f(kDecay), 0)), 0.0,
                                     static_cast<f64>(kShakeMaxDecay));
        for (u32 i = 0; i < count; ++i) {
            const f64 dt = count == 1 ? 0 : ((i + .5) / count - .5) * blur / fps;
            // O decaimento entra na intensidade: x, y, giro e zoom caem juntos.
            shake::Settings si = s;
            si.amount *= static_cast<f32>(std::exp(-decay * std::max(0.0, seconds + dt)));
            const auto pose = shake::sample(si, seconds + dt);
            const f32 px = pose.x * dirC - pose.y * dirS, py = pose.x * dirS + pose.y * dirC;
            const f32 angle = pose.rotation * kDeg2Rad;
            const f32 a = std::cos(angle)/pose.scale, c = std::sin(angle)/pose.scale;
            const f32 b = -c, d = a;
            const f32 tx = cx-a*(cx+px)-c*(cy+py);
            const f32 ty = cy-b*(cx+px)-d*(cy+py);
            u.rows[i*2] = {a, c*in.h/in.w, (a*in.x+c*in.y+tx-in.x)/in.w, 0};
            u.rows[i*2+1] = {b*in.w/in.h, d, (b*in.x+d*in.y+ty-in.y)/in.h, 0};
        }
        u.options = {static_cast<f32>(count), std::clamp(finite_or(e.f(kMix), 100)/100.f, 0.f, 1.f), static_cast<f32>(e.e(kEdges)), 0};
        u.bounds = {.5f/input.width, .5f/input.height, 0, 0};
        out = input;
        out.texture = ctx.texture("shake", input.width, input.height);
        return ctx.fullscreen_pass("shake", PassStage::Effects, out.texture, ShaderId::effects_camera_shake_frag,
            {PassTexture{input.texture, {}, CommonSampler::LinearClamp}}, &u, sizeof(u)) == kInvalidIndex
            ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Turbulência (deslocamento procedural)
// -----------------------------------------------------------------------------
class Turbulence final : public Effect {
public:
    // Novos sempre no FIM: o projeto salvo guarda os parâmetros por índice.
    enum : u32 { kAmount = 0, kSize, kComplexity, kEvolution, kOffsetX, kOffsetY, kSeed,
                 kHorizontal, kEdges, kSpin, kMix, kPin, kType, kCycleOn, kCycle };
    static constexpr u32 kTypeCount = 9, kPinCount = 11, kLegacyPinLast = 5, kMaxCycle = 30;

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTurbulence, "Turbulência", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kEdgeModes[] = {"Repetir", "Recortar", "Esticar"};
        // Digitado ~10x o slider. O laço do shader é fixo (6 oitavas); a margem
        // cresce com a intensidade, mas a textura fica presa ao quadro visível
        // e ao teto do aparelho. A complexidade (oitavas = iterações) não alarga.
        p.add_float("amount", "Intensidade", 40.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_float("size", "Tamanho do ruído", 120.0f, 2.0f, 2000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(2.0f, 20000.0f);
        p.add_float("complexity", "Complexidade", 3.0f, 1.0f, 6.0f, kParamAnimatable, "oitavas");
        p.add_float("evolution", "Evolução", 0.0f, -360000.0f, 360000.0f, kParamAnimatable, "°");
        p.add_float("offset_x", "Deslocamento X", 0.0f, -1000.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(-100000.0f, 100000.0f);
        p.add_float("offset_y", "Deslocamento Y", 0.0f, -1000.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(-100000.0f, 100000.0f);
        p.add_int("seed", "Semente", 3, 0, 9999);
        p.add_bool("horizontal_only", "Só na horizontal", false);
        p.add_enum("edges", "Bordas", kEdgeModes, 3, 1);
        p.add_angle("spin", "Girar o deslocamento", 0.0f);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        // 0..5 são os de sempre (seguram o vetor inteiro a 12% da borda). Os
        // novos vão no fim, medidos em 2x a intensidade: sem trava só segura o
        // movimento que atravessa a borda; travada segura as duas direções.
        static const char* const pins[kPinCount] = {"Nenhuma", "Todas", "Esquerda", "Direita", "Acima", "Abaixo",
            "Bordas horizontais", "Bordas verticais", "Todas travadas", "Bordas horizontais travadas",
            "Bordas verticais travadas"};
        p.add_enum("pinning", "Fixar bordas", pins, kPinCount, 0);
        // O padrão (Turbulento, sem ciclo) é o campo de antes, bit a bit.
        static const char* const types[kTypeCount] = {"Turbulento", "Protuberância", "Torção",
            "Turbulento mais suave", "Protuberância mais suave", "Torção mais suave", "Vertical", "Horizontal",
            "Cruzado"};
        p.add_enum("displacement", "Tipo de deslocamento", types, kTypeCount, 0);
        p.add_bool("cycle_evolution", "Ciclo de evolução", false);
        p.add_int("cycle", "Ciclo (em revoluções)", 1, 1, static_cast<i32>(kMaxCycle));
        // Seções do painel (só apresentação; índices intactos).
        p.add_section("cycle", "Ciclo de evolução", {kCycle}, kCycleOn, false);
        p.add_section("offset", "Deslocamento", {kOffsetX, kOffsetY, kSpin}, kInvalidIndex, false);
        p.add_section("edges", "Bordas", {kHorizontal, kEdges, kPin}, kInvalidIndex, false);
        p.add_section("output", "Saída", {kSeed, kMix}, kInvalidIndex, false);
        p.set_tip(kPin, "Fixar segura a imagem junto à borda da camada.");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f || e.f(kAmount) < 0.01f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return e.f(kAmount);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmount] = ParamValue::scalar(55.0f);
        v[kSize] = ParamValue::scalar(90.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = e.f(kAmount);
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{1.0f/static_cast<f32>(input.width), 1.0f/static_cast<f32>(input.height),
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kAmount), e.f(kSize), e.f(kComplexity), e.f(kEvolution)};
        u.p1 = Vec4{e.f(kOffsetX), e.f(kOffsetY), static_cast<f32>(e.e(kSeed)), e.b(kHorizontal) ? 1.0f : 0.0f};
        const u32 pin = std::min(e.e(kPin), kPinCount - 1);
        u.p2 = Vec4{static_cast<f32>(e.e(kEdges)), e.f(kSpin), e.f(kMix)/100.0f, static_cast<f32>(pin)};
        // Ciclo: N revoluções da evolução = N células no eixo do tempo. O tempo
        // já vai dobrado em [0, N) em double — a evolução 0 e a N*360 caem no
        // MESMO ponto, sem o arredondamento da GPU, e evolução grande não perde
        // precisão.
        const u32 type = e.count > kType ? std::min(e.e(kType), kTypeCount - 1) : 0u;
        const bool cycleOn = e.count > kCycle && e.b(kCycleOn);
        const u32 cycle = cycleOn ? std::clamp(e.e(kCycle), 1u, kMaxCycle) : 0u;
        f64 cycleZ = 0.0;
        if (cycle > 0) {
            const f64 turns = static_cast<f64>(finite_or(e.f(kEvolution), 0.0f)) / 360.0;
            cycleZ = std::fmod(turns, static_cast<f64>(cycle));
            if (cycleZ < 0.0) cycleZ += static_cast<f64>(cycle);
            if (!(cycleZ > 0.0 && cycleZ < static_cast<f64>(cycle))) cycleZ = 0.0;   // -0, N e NaN
        }
        u.p3 = Vec4{static_cast<f32>(e.time_frames()), finite_or(e.f(kComplexity), 3.0f),
                    static_cast<f32>(type), static_cast<f32>(cycle)};
        u.color = Vec4{static_cast<f32>(cycleZ), 0.0f, 0.0f, 0.0f};   // p4 no shader
        // O caso de sempre (Turbulento, sem ciclo, fixações 0..5) fica no
        // shader de sempre, sem uma linha mudada: o mesmo SPIR-V é o que deixa
        // o projeto antigo bit a bit igual em qualquer GPU. O resto vai para o
        // shader dos tipos, com os mesmos uniforms.
        const bool legacy = type == 0 && cycle == 0 && pin <= kLegacyPinLast;
        const ShaderId shader = legacy ? ShaderId::effects_turbulence_displace_frag
                                       : ShaderId::effects_turbulence_types_frag;

        out = LayerImage{ctx.texture("turbulencia", w, h), region, w, h};
        if (ctx.fullscreen_pass("turbulencia", PassStage::Transform, out.texture, shader,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        (void)e.f(kMix);
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Onda
// -----------------------------------------------------------------------------
class SineWaves final : public Effect {
public:
    enum : u32 { kAmplitude, kFrequency, kAngle, kDisplacementAngle, kPhase, kSpeed,
                 kDistance, kWrapX, kWrapY, kFilter, kMatte, kMatteChannel, kInvertMatte };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.sine_waves", "Ondas senoidais", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const wrap[] = {"Transparente", "Repetir", "Espelhar"};
        static const char* const matte[] = {"Luminância", "Alfa"};
        p.add_float("amplitude", "Amplitude", 10, -100, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("frequency", "Frequência", 8, .1f, 64);
        p.typed_range(.1f, 256);
        p.add_angle("angle", "Ângulo da onda", 45);
        p.add_angle("displacement_angle", "Ângulo do deslocamento", 90);
        p.add_angle("phase", "Fase", 0);
        p.add_float("speed", "Velocidade de fase", 0, -20, 20, kParamAnimatable, "Hz");
        p.add_float("distance", "Distância", 1, .05f, 5);
        p.add_enum("wrap_x", "Borda horizontal", wrap, 3, 2);
        p.add_enum("wrap_y", "Borda vertical", wrap, 3, 2);
        p.add_bool("filter", "Filtragem adaptativa", true);
        p.add_layer_ref("matte", "Camada do matte");
        p.add_enum("matte_channel", "Canal do matte", matte, 2, 0);
        p.add_bool("invert_matte", "Inverter matte", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::abs(e.f(kAmplitude)) < 1e-5f && std::abs(e.f(kDistance) - 1.0f) < 1e-5f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_sine_waves_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        const u64 matteRef = e.value(kMatte).ref;
        const LayerImage* matte = matteRef ? ctx.layer_input(matteRef) : nullptr;
        if (matteRef && !matte) { out = input; return OkStatus; }
        const f32 angle = std::remainder(finite_or(e.f(kAngle), 45.0f), 360.0f) * kDeg2Rad;
        const f32 shift = angle - std::remainder(finite_or(e.f(kDisplacementAngle), 90.0f), 360.0f) * kDeg2Rad;
        const f64 seconds = e.time_frames() / std::max(e.framesPerSecond, 1e-6);
        const f32 phase = static_cast<f32>(std::remainder(seconds * finite_or(e.f(kSpeed), 0.0f) +
            finite_or(e.f(kPhase), 0.0f) / 360.0, 1.0));
        struct Uniforms { EffectUniforms base; Vec4 inputOrigin; Mat4 compFromLayer; } u{};
        u.base = base_uniforms(input);
        u.base.p0 = {std::cos(angle), -std::sin(angle), std::cos(shift), -std::sin(shift)};
        u.base.p1 = {finite_or(e.f(kAmplitude), 0.0f) * .005f, 2.0f * finite_or(e.f(kFrequency), 8.0f),
                phase, std::max(.05f, finite_or(e.f(kDistance), 1.0f))};
        u.base.p2 = {f32(e.e(kWrapX)), f32(e.e(kWrapY)), e.b(kFilter) ? 1.0f : 0.0f, matte ? 1.0f : 0.0f};
        u.base.p3 = {input.region.w, input.region.h, f32(e.e(kMatteChannel)), e.b(kInvertMatte) ? 1.0f : 0.0f};
        if (matte) u.base.color = {matte->region.x, matte->region.y, matte->region.w, matte->region.h};
        u.inputOrigin = {input.region.x, input.region.y, 0, 0};
        u.compFromLayer = e.placement ? e.placement->compFromLayer : Mat4::identity();
        out = input;
        out.texture = ctx.texture("sine-waves", input.width, input.height);
        if (ctx.fullscreen_pass("sine-waves", PassStage::Transform, out.texture, ShaderId::effects_sine_waves_frag,
                                {PassTexture{input.texture}, PassTexture{matte ? matte->texture : input.texture}},
                                &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

class WaveWarp final : public Effect {
public:
    enum : u32 { kHeight = 0, kWavelength, kSpeed, kPhase, kDirection, kSquare, kEdges, kPin, kMix,
                 kDirectionAngle, kWaveType };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kWaveWarp, "Onda", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kDirs[] = {"Horizontal", "Vertical", "Diagonal", "As duas", "Ângulo"};
        static const char* const kEdgeModes[] = {"Repetir", "Recortar", "Esticar"};
        // Digitado ~10x o slider: uma amostra por pixel, custo fixo.
        p.add_float("height", "Altura da onda", 30.0f, 0.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 10000.0f);
        p.add_float("wavelength", "Largura de onda", 200.0f, 2.0f, 4000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(2.0f, 40000.0f);
        p.add_float("speed", "Velocidade", 0.0f, -200.0f, 200.0f, kParamAnimatable | kParamPixels | kParamPerFrame, "px/quadro");
        p.typed_range(-5000.0f, 5000.0f);
        p.add_angle("phase", "Fase", 0.0f);
        p.add_enum("direction", "Direção", kDirs, 5, 0);
        p.add_bool("square", "Onda quadrada", false);
        p.add_enum("edges", "Bordas", kEdgeModes, 3, 1);
        p.add_bool("pin_edges", "Travar nas bordas", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        // Append, never reinterpret the four direction values saved before
        // this control existed. It is read only by the new angular mode.
        p.add_angle("direction_angle", "Ângulo", 0.0f, -360.0f, 360.0f);
        // AE 2026 (no fim): os tipos de onda. "Seno" (o padrão) respeita a
        // chave "Onda quadrada" de antes; os outros mandam sobre ela.
        static const char* const kTypes[] = {"Seno", "Quadrada", "Triângulo", "Dente de serra", "Círculo",
                                             "Semicírculo", "Semicírculo invertido", "Ruído", "Ruído suave"};
        p.add_enum("wave_type", "Tipo de onda", kTypes, 9, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f || e.f(kHeight) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return e.f(kHeight); }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHeight] = ParamValue::scalar(36.0f);
        v[kWavelength] = ParamValue::scalar(140.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 h = e.f(kHeight);
        const Rect region = spread_region(input.region, h, h, e.placement, margin);
        u32 w = 0, hh = 0;
        ctx.region_size(region, input.texel_scale_x(), w, hh);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{h, e.f(kWavelength), e.f(kSpeed), e.f(kPhase)};
        const u32 waveType = e.count > kWaveType ? std::min<u32>(e.e(kWaveType), 8u) : 0u;
        u.p1 = Vec4{static_cast<f32>(e.e(kDirection)), e.b(kSquare) || waveType == 1u ? 1.0f : 0.0f,
                    static_cast<f32>(e.e(kEdges)), e.b(kPin) ? 1.0f : 0.0f};
        // p3.y = tipo de onda novo (2..8); 0 = seno/quadrada como antes.
        u.p3 = Vec4{static_cast<f32>(e.time_frames()), waveType >= 2u ? static_cast<f32>(waveType) : 0.0f, 0.0f, 0.0f};
        const f32 angle = e.count > kDirectionAngle && std::isfinite(e.f(kDirectionAngle))
            ? std::remainder(e.f(kDirectionAngle), 360.0f) * kDeg2Rad : 0.0f;
        u.p2 = Vec4{std::cos(angle), std::sin(angle), input.region.x, input.region.y};

        out = LayerImage{ctx.texture("onda", w, hh), region, w, hh};
        if (ctx.fullscreen_pass("onda", PassStage::Transform, out.texture, ShaderId::effects_wave_warp_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Lente (warp)
// -----------------------------------------------------------------------------
class Warp final : public Effect {
public:
    enum : u32 { kMode = 0, kAmount, kRadius, kCenter, kEdges, kMix, kSphereLight };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kWarp, "Lente", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kModes[] = {"Empurrar", "Puxar", "Torcer", "Esfera", "Canto"};
        static const char* const kEdgeModes[] = {"Repetir", "Recortar", "Esticar"};
        p.add_enum("mode", "Modo", kModes, 5, 0);
        // Digitado ~10x o slider: uma amostra por pixel, custo fixo.
        p.add_float("amount", "Intensidade", 60.0f, -1000.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(-10000.0f, 10000.0f);
        p.add_float("radius", "Raio", 260.0f, 1.0f, 4000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(1.0f, 40000.0f);
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_enum("edges", "Bordas", kEdgeModes, 3, 1);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("sphere_light", "Luz da esfera", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f || std::fabs(e.f(kAmount)) < 0.01f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::fabs(e.f(kAmount)); }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kMode] = ParamValue::scalar(3.0f);       // esfera: a mais reconhecível
        v[kAmount] = ParamValue::scalar(55.0f);
        v[kRadius] = ParamValue::scalar(220.0f);
        v[kSphereLight] = ParamValue::scalar(60.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = std::fabs(e.f(kAmount));
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        // O centro é relativo à LAYER; o shader o converte para o espaço da
        // textura de saída dividindo por texel scale.
        const Vec2 c = e.p2(kCenter);
        u.p0 = Vec4{static_cast<f32>(e.e(kMode)), e.f(kAmount), e.f(kRadius), e.f(kSphereLight) / 100.0f};
        u.p1 = Vec4{c.x, c.y, static_cast<f32>(e.e(kEdges)), e.f(kMix) / 100.0f};

        out = LayerImage{ctx.texture("lente", w, h), region, w, h};
        if (ctx.fullscreen_pass("lente", PassStage::Transform, out.texture, ShaderId::effects_warp_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Ondulação que dissolve
// -----------------------------------------------------------------------------
class RippleDissolve final : public Effect {
public:
    enum : u32 { kProgress = 0, kAmplitude, kWavelength, kSoftness, kCenter, kSpeed, kSeed,
                 kDistort, kInvert, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kRippleDissolve, "Ondulação que dissolve", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("progress", "Progresso", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        // Digitado alargado: a margem é 9x a ondulação (presa ao quadro
        // visível), por isso a ondulação para em 4x o slider; o resto ~10x.
        p.add_float("amplitude", "Ondulação", 25.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("wavelength", "Comprimento da onda", 120.0f, 4.0f, 2000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(4.0f, 20000.0f);
        p.add_float("softness", "Suavidade da borda", 15.0f, 1.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("speed", "Velocidade da onda", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.typed_range(-1000.0f, 1000.0f);
        p.add_int("seed", "Semente", 2, 0, 9999);
        p.add_bool("warp_image", "Distorcer a imagem junto", true);
        p.add_bool("outside_in", "De fora para dentro", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f || e.f(kProgress) < 0.001f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return e.b(kDistort) ? e.f(kAmplitude) * 9.0f : 0.0f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kProgress] = ParamValue::scalar(55.0f);
        v[kAmplitude] = ParamValue::scalar(38.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = e.b(kDistort) ? e.f(kAmplitude) * 9.0f : 0.0f;
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        const Vec2 c = e.p2(kCenter);
        // O progresso entra como RAIO: 0 não dissolver nada, 1 dissolver tudo.
        // A suavidade é a meia-largura da transição, e o padrão do joelho é
        // uma fração do progresso para ele não ficar duro no começo.
        const f32 progress = std::clamp(e.f(kProgress) / 100.0f, 0.0f, 1.0f);
        const f32 soft = std::max(0.001f, e.f(kSoftness) / 100.0f * 0.5f);
        u.p0 = Vec4{progress * 1.05f - 0.02f, e.f(kAmplitude), e.f(kWavelength), soft};
        u.p1 = Vec4{c.x, c.y, e.f(kSpeed), static_cast<f32>(e.e(kSeed))};
        u.p2 = Vec4{e.b(kDistort) ? 1.0f : 0.0f, e.b(kInvert) ? 1.0f : 0.0f, 0.0f, 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.time_frames()), 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("ondulacao", w, h), region, w, h};
        if (ctx.fullscreen_pass("ondulacao", PassStage::Transform, out.texture,
                                ShaderId::effects_ripple_dissolve_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        (void)e.f(kMix);
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Bojo — o raio estufa (+) ou pinça (-) em volta de um centro. Irmão pobre da
// Lente, e é de propósito que os dois existem: a Lente empurra/puxa/espelha uma
// calota com brilho, o Bojo é só o remapeamento radial puro, com a mesma curva
// contínua na borda do antigo.
// -----------------------------------------------------------------------------
class Bulge final : public Effect {
public:
    enum : u32 { kCenter = 0, kRadius, kHeight, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kBulge, "Bojo", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        // O raio em fração da ALTURA do quadro: ele é um círculo, não uma
        // elipse esticada com a largura.
        p.add_float("radius", "Raio", 30.0f, 1.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.1f, 2000.0f);
        // -95% pinça (o centro encolhe), +95% estufa (o centro cresce).
        p.add_float("height", "Altura", 50.0f, -95.0f, 95.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(e.f(kHeight)) < 0.01f || e.f(kMix) < 0.01f || e.f(kRadius) <= 0.0f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kRadius] = ParamValue::scalar(38.0f);
        v[kHeight] = ParamValue::scalar(75.0f);
        return true;
    }
    f32 input_margin(const EffectEval&) const noexcept override { return 0.0f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_bulge_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        const Vec2 c = e.p2(kCenter);
        // O raio chega em % da ALTURA da camada e vira uv de altura: em uv, a
        // altura é 1, então 30% = 0.30. O shader cuida da razão de aspecto.
        u.p0 = Vec4{c.x, c.y, std::clamp(e.f(kRadius) * 0.01f, 1e-4f, 20.0f), 0.0f};
        u.p1 = Vec4{std::clamp(e.f(kHeight) * 0.01f, -0.95f, 0.95f),
                    std::clamp(e.f(kMix) * 0.01f, 0.0f, 1.0f), 0.0f, 0.0f};
        out = input;
        out.texture = ctx.texture("bojo", input.width, input.height);
        // Borda transparente: o que a deformação puxa de fora do quadro some,
        // em vez de esticar a última linha de pixels.
        if (ctx.fullscreen_pass("bojo", PassStage::Transform, out.texture, ShaderId::effects_bulge_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

class TurbulentDisplaceReference final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{"aurea.distort.turbulent_displace_reference", "Turbulent Displace", "Distorcer", EffectClass::Domain};
        return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* types[]{"Turbulento", "Protuberância", "Torção", "Turbulento suave", "Protuberância suave",
            "Torção suave", "Vertical", "Horizontal", "Cruzado"};
        static const char* pins[]{"Nenhuma", "Todas", "Bordas horizontais", "Bordas verticais", "Esquerda", "Direita",
            "Acima", "Abaixo", "Todas travadas", "Horizontais travadas", "Verticais travadas", "Esquerda travada",
            "Direita travada", "Acima travada", "Abaixo travada"};
        static const char* aa[]{"Normal", "Alta"};
        p.add_enum("displacement", "Deslocamento", types, 9, 0);
        p.add_float("amount", "Valor", 50, -100, 100, kParamAnimatable | kParamPixels, "px");
        p.typed_range(-10000, 10000);
        p.add_float("size", "Tamanho", 100, 2, 1000, kParamAnimatable | kParamPixels, "px");
        p.add_point2("offset", "Deslocar turbulência", {.5f, .5f}, -100, 100, kParamAnimatable | kParamRelative);
        p.add_float("complexity", "Complexidade", 1, 1, 10);
        p.add_angle("evolution", "Evolução", 0);
        p.add_bool("cycle_evolution", "Evolução do ciclo", false);
        p.add_int("cycle", "Ciclo em revoluções", 1, 1, 88);
        p.add_int("seed", "Distribuição aleatória", 0, 0, 100000);
        p.add_enum("pinning", "Fixação", pins, 15, 1);
        p.add_bool("resize", "Redimensionar camada", false);
        p.add_enum("antialias", "Suavização de borda", aa, 2, 0);
        p.add_section_range("evolution", "Opções de evolução", 6, 8, kInvalidIndex, false);
        p.set_enabled_by(7, 6, 1u);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return std::fabs(finite_or(e.f(1), 0.0f)) < 1e-6f; }
    bool needs_full_input() const noexcept override { return true; }
    f32 input_margin(const EffectEval& e) const noexcept override {
        const u32 type = std::min(e.e(0), 8u);
        const f32 interpolationBound = type >= 3 && type <= 5 ? 1.728f : 1.2f;
        const f32 differenceBound = type == 1 || type == 2 || type == 4 || type == 5 ? 2.0f : 1.0f;
        const f32 amplitude = std::fabs(finite_or(e.f(1), 0.0f)) * std::clamp(finite_or(e.f(2), 100.0f), 2.0f, 1000.0f) * .05f;
        return amplitude * octave_gain(e.f(4)) * interpolationBound * differenceBound;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_turbulent_displace_reference_frag, format));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        if (!e.resources) return;
        const i64 angle = static_cast<i64>(std::llround(std::clamp(static_cast<f64>(finite_or(e.f(5), 0.0f)), -32768.0, 32767.0) * 65536));
        const u32 seed = static_cast<u32>(std::clamp(finite_or(e.f(8), 0.0f), 0.0f, 100000.0f));
        const u32 cycle = e.b(6) ? std::clamp(e.e(7), 1u, 88u) : 90u;
        const auto lattice = noise_lattice(angle, seed, cycle);
        u64 key = (0x545552424c415431ull ^ static_cast<u64>(angle)) * 1099511628211ull;
        key = ((key ^ seed) * 1099511628211ull) ^ cycle;
        e.aux = e.resources->data_texture(key, lattice->data(), 64, 64);
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        if (!e.aux.valid()) return Status{Errc::Busy, "Turbulent Displace lattice pending"};
        const u32 type = std::min(e.e(0), 8u), pin = std::min(e.e(9), 14u);
        const f32 size = std::clamp(finite_or(e.f(2), 100.0f), 2.0f, 1000.0f);
        const f32 amplitude = finite_or(e.f(1), 0.0f) * size * .05f;
        const bool resize = e.b(10) && pin != 1 && pin != 8;
        const f32 reach = resize ? input_margin(e) : 0;
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 width = 0, height = 0;
        ctx.region_size(region, input.texel_scale_x(), width, height);
        const f32 layerWidth = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 layerHeight = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        const f32 complexity = std::clamp(finite_or(e.f(4), 1.0f), 1.0f, 10.0f);
        const f32 gain = octave_gain(complexity);
        const f32 pinReach = std::max(0.0f, gain * amplitude * (4.0f / 3.0f));
        const Vec2 point = e.p2(3);
        const Vec2 offset{finite_or(point.x, .5f), finite_or(point.y, .5f)};
        struct Uniforms {
            Vec4 region, source, shape, offset, pin, quality;
            std::array<Vec4, 30> coordinates;
            std::array<Vec4, 10> octaveScale;
        } u{};
        u.region = {region.x, region.y, region.w, region.h};
        u.source = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.shape = {amplitude, (.5f / size) * (type < 3 ? .35355339f : 1.0f), complexity, static_cast<f32>(type)};
        const f64 anchorX = std::floor(static_cast<f64>(layerWidth) * .5);
        const f64 anchorY = std::floor(static_cast<f64>(layerHeight) * .5);
        u.offset = {static_cast<f32>(anchorX), static_cast<f32>(anchorY), layerWidth, layerHeight};
        // The large lattice biases must retain their fractional parts through
        // every octave. Evaluate them in double precision on the CPU, then let
        // the shader add only the local displacement from the layer center.
        const f64 scale = (.5 / static_cast<f64>(size)) * (type < 3 ? .3535533905932738 : 1.0);
        constexpr std::array<std::array<f64, 2>, 3> branches{{{0, 0}, {37, 17}, {0, .25}}};
        for (u32 branch = 0; branch < branches.size(); ++branch) {
            f64 x = (anchorX - static_cast<f64>(offset.x) * layerWidth) * scale + 7913.17;
            f64 y = (anchorY - static_cast<f64>(offset.y) * layerHeight) * scale + 9711.73;
            if (type == 1 || type == 2 || type == 4 || type == 5) {
                if (branch == 1) x += .25;
                if (branch == 2) y += .25;
            } else {
                x += branches[branch][0];
                y += branches[branch][1];
            }
            if (type < 3) {
                const f64 originalX = x;
                x -= y;
                y += originalX;
            }
            f64 frequency = scale;
            for (u32 octave = 0; octave < u.octaveScale.size(); ++octave) {
                x *= 1.77;
                y *= 1.77;
                frequency *= 1.77;
                const f64 cellX = std::floor(x), cellY = std::floor(y);
                // The XOR corner permutation consumes all twelve low cell
                // bits. Its exact period is 4096, including the high six bits
                // mixed into the other axis. A 64-cell wrap changes the hash.
                const f64 wrappedX = cellX - std::floor(cellX / 4096) * 4096;
                const f64 wrappedY = cellY - std::floor(cellY / 4096) * 4096;
                u.coordinates[branch * 10 + octave] = {
                    static_cast<f32>(x - cellX), static_cast<f32>(y - cellY),
                    static_cast<f32>(wrappedX), static_cast<f32>(wrappedY)};
                u.octaveScale[octave].x = static_cast<f32>(frequency);
            }
        }
        u.pin = {static_cast<f32>(pin), std::min(layerWidth * .5f, pinReach), std::min(layerHeight * .5f, pinReach), 0};
        u.quality = {e.e(11) == 1 ? 2.0f : 1.0f, region.w / width, region.h / height, 0};
        out = {ctx.texture("turbulent-displace-reference", width, height), region, width, height};
        if (ctx.fullscreen_pass("turbulent-displace-reference", PassStage::Transform, out.texture,
                ShaderId::effects_turbulent_displace_reference_frag,
                {{input.texture, {}, CommonSampler::LinearBorder}, {{}, e.aux, CommonSampler::NearestClamp}},
                &u, sizeof(u)) == kInvalidIndex) return Errc::PipelineCompileFailed;
        return OkStatus;
    }
private:
    using Lattice = std::array<Vec4, 4096>;
    static f32 octave_gain(f32 complexity) noexcept {
        complexity = std::clamp(finite_or(complexity, 1.0f), 1.0f, 10.0f);
        const f32 full = std::floor(complexity);
        const f32 next = std::pow(.7f, full);
        return .25f * ((1 - next) / .3f + (complexity - full) * next);
    }
    static std::shared_ptr<const Lattice> noise_lattice(i64 angle, u32 seed, u32 cycle) noexcept {
        struct Entry { i64 angle; u32 seed, cycle; std::shared_ptr<const Lattice> data; };
        static std::mutex mutex;
        static std::vector<Entry> cache;
        std::lock_guard lock(mutex);
        for (const auto& entry : cache) if (entry.angle == angle && entry.seed == seed && entry.cycle == cycle) return entry.data;
        constexpr i64 quarter = 90ll * 65536;
        const i64 base = static_cast<i64>(std::floor(static_cast<f64>(angle) / quarter)) * quarter + seed;
        const f64 fraction = static_cast<f64>(angle + seed - base) / quarter;
        const i64 period = static_cast<i64>(cycle) * quarter * 4;
        std::array<u32, 6> streams{};
        for (i64 index = 0; index < 6; ++index) {
            const i64 value = ((base + (index - 2) * quarter) % period + period) % period;
            streams[index] = static_cast<u32>(value) * static_cast<u32>(-341751747) + 229283573u;
        }
        auto lattice = std::make_shared<Lattice>();
        u32 phaseStream = 12356789;
        for (Vec4& point : *lattice) {
            phaseStream = phaseStream * 1103515245u + 12345u;
            f64 phase = static_cast<f64>((phaseStream >> 16) & 32767) / 16384 - 1 + fraction;
            std::array<f64, 6> values{};
            for (u32 index = 0; index < streams.size(); ++index) {
                streams[index] = streams[index] * 1103515245u + 12345u;
                values[index] = static_cast<f64>((streams[index] >> 16) & 32767) / 16384 - 1;
            }
            u32 start = 1;
            if (phase < 0) { phase += 1; start = 0; }
            else if (phase > 1) { phase -= 1; start = 2; }
            const f64 square = phase * phase, cube = square * phase, inverse = 1 - phase;
            const f64 value = (inverse * inverse * inverse * values[start]
                + (3 * cube - 6 * square + 4) * values[start + 1]
                + (-3 * cube + 3 * square + 3 * phase + 1) * values[start + 2]
                + cube * values[start + 3]) * .2;
            point.x = static_cast<f32>(value);
        }
        if (cache.size() == 4) cache.erase(cache.begin());
        cache.push_back({angle, seed, cycle, lattice});
        return lattice;
    }
};

class WaveWarpReference final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{effect_keys::kWaveWarpReference, "Wave Warp", "Distorcer", EffectClass::Domain};
        return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* waves[]{"Senoidal", "Quadrada", "Triangular", "Dente de serra", "Círculo",
            "Semicírculo", "Semicírculo inverso", "Ruído", "Ruído suave"};
        static const char* pins[]{"Nenhuma", "Todas as bordas", "Centro", "Esquerda", "Acima", "Direita",
            "Abaixo", "Bordas horizontais", "Bordas verticais"};
        static const char* antialias[]{"2 × 2", "3 × 3", "4 × 4"};
        p.add_enum("wave_type", "Tipo de onda", waves, 9, 0);
        p.add_float("height", "Altura da onda", 10, -100, 100, kParamAnimatable | kParamPixels, "px");
        p.typed_range(-32000, 32000);
        p.add_float("width", "Largura da onda", 40, 1, 1000, kParamAnimatable | kParamPixels, "px");
        p.typed_range(1, 32000);
        p.add_angle("direction", "Direção", 90);
        p.add_float("speed", "Velocidade da onda", 1, -10, 10, kParamAnimatable, "Hz");
        p.typed_range(-100, 100);
        p.add_enum("pinning", "Fixação", pins, 9, 0);
        p.add_angle("phase", "Fase", 0);
        p.add_enum("antialias", "Suavização de bordas", antialias, 3, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return std::fabs(e.f(1)) < 1e-6f; }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_wave_warp_reference_frag, format));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        if (!e.resources) return;
        const u32 wave = std::min(e.e(0), 8u);
        const auto& table = wave_tables()[wave];
        e.aux = e.resources->data_texture(0x5756415645300000ull + wave, table.data(), 2048, 1);
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        if (!e.aux.valid()) return Status{Errc::Busy, "Wave Warp waveform pending"};
        EffectUniforms u = base_uniforms(input);
        const f64 seconds = e.time_frames() / std::max(e.framesPerSecond, 1e-6);
        // The reference CPU renderer adds the AngleParamSuite value directly
        // to its table index. Its phase control therefore advances one sample
        // per displayed degree, independently of the 2048-sample time cycle.
        const f64 phase = e.f(6) - seconds * e.f(4) * 2048.0;
        const f32 width = std::clamp(e.f(2), 1.0f, 32000.0f);
        const f64 angle = (static_cast<f64>(e.f(3)) - 90) * 3.14159265358979323846 / 180;
        const u32 samples = std::min(e.e(7), 2u) + 2;
        // 16.16 waveform accumulator (AE 2027 oracle, all three AA levels):
        // round(65536 * 1024 / (width * N)) per high-resolution pixel. The
        // values exceed float precision, so they travel as 16-bit halves.
        const f64 base = 65536.0 * 1024.0 / (static_cast<f64>(width) * samples);
        const auto split = [](i64 value, f32& high, f32& low) {
            const i64 h = value >= 0 ? value / 65536 : -((-value + 65535) / 65536);
            high = static_cast<f32>(h);
            low = static_cast<f32>(value - h * 65536);
        };
        f64 wrapped = std::fmod(phase, 2048.0);
        if (!std::isfinite(wrapped)) wrapped = 0;
        if (wrapped < 0) wrapped += 2048.0;
        u.p0 = {e.f(1), width, static_cast<f32>(angle), static_cast<f32>(wrapped)};
        u.p1 = {static_cast<f32>(std::min(e.e(5), 8u)), static_cast<f32>(samples), 0, 0};
        split(static_cast<i64>(std::llround(wrapped * 65536)) % (i64{2048} * 65536), u.p1.z, u.p1.w);
        split(std::llround(base * std::cos(angle)), u.color.x, u.color.y);
        split(std::llround(base * std::sin(angle)), u.color.z, u.color.w);
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.p3 = {e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w,
                e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h, 0, 0};
        out = input;
        out.texture = ctx.texture("wave-warp-reference", input.width, input.height);
        if (ctx.fullscreen_pass("wave-warp-reference", PassStage::Transform, out.texture,
                ShaderId::effects_wave_warp_reference_frag,
                {{input.texture, {}, CommonSampler::LinearBorder}, {{}, e.aux, CommonSampler::NearestClamp}},
                &u, sizeof(u)) == kInvalidIndex) return Errc::PipelineCompileFailed;
        return OkStatus;
    }
private:
    using WaveTable = std::array<Vec4, 2048>;
    static const std::array<WaveTable, 9>& wave_tables() noexcept {
        static const std::array<WaveTable, 9> tables = [] {
            std::array<WaveTable, 9> result{};
            std::array<i32, 256> noise{};
            u32 state = 17245;
            for (i32& value : noise) {
                // Deterministic integer recurrence specified by the observed
                // waveform. Quotient/remainder avoid signed overflow.
                state = 48271u * (state % 44488u) + 3399u * (state / 44488u);
                if (!state) state = 2147483647u;
                value = static_cast<i32>((state >> 7) & 32767u) * 4 - 65536;
            }
            auto smooth = noise;
            for (u32 pass = 0; pass < 20; ++pass) {
                auto previous = smooth;
                for (u32 index = 0; index < smooth.size(); ++index) {
                    const i32 sum = previous[(index + 255) % 256] + previous[index] + previous[(index + 1) % 256];
                    smooth[index] = sum / 3;
                }
            }
            for (u32 index = 0; index < 2048; ++index) {
                const f64 phase = static_cast<f64>(index) / 2048;
                const f64 arc = 2 * std::fmod(phase * 2, 1.0) - 1;
                const f64 semicircle = std::sqrt(std::max(0.0, 1 - arc * arc));
                const f64 fullArc = 2 * phase - 1;
                const f64 values[]{std::sin(phase * 6.283185307179586), phase < .5 ? 1.0 : -1.0,
                    1 - 4 * std::fabs(phase - .5), 2 * phase - 1,
                    phase < .5 ? semicircle : -semicircle,
                    1 - 2 * std::sqrt(std::max(0.0, 1 - fullArc * fullArc)),
                    phase < .5 ? 1 - semicircle : semicircle - 1};
                for (u32 wave = 0; wave < 7; ++wave) {
                    result[wave][index].x = static_cast<f32>(std::round(values[wave] * 65536) / 65536);
                }
                result[7][index].x = static_cast<f32>(noise[index / 8]) / 65536;
                const u32 start = index / 8;
                const f32 fraction = static_cast<f32>(index % 8) / 8;
                result[8][index].x = (smooth[start] * (1 - fraction) + smooth[(start + 1) % 256] * fraction) / 65536;
            }
            return result;
        }();
        return tables;
    }
};

} // namespace

void register_distort_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Shake>());
    (void)r.add(std::make_unique<Turbulence>());
    (void)r.add(std::make_unique<WaveWarp>());
    (void)r.add(std::make_unique<WaveWarpReference>());
    (void)r.add(std::make_unique<SineWaves>());
    (void)r.add(std::make_unique<TurbulentDisplaceReference>());
    (void)r.add(std::make_unique<Warp>());
    (void)r.add(std::make_unique<RippleDissolve>());
    (void)r.add(std::make_unique<Bulge>());
}

} // namespace aurea::builtin
