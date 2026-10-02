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
#include <cmath>

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
                 kAmount, kZoom, kStyle, kBlur, kPhase, kEdges, kWave, kDirection, kDecay };
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
        const f64 earliest = static_cast<f64>(e.localTime.value) / fps - kShakeMaxShutter / fps;
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
        const f64 fps = e.framesPerSecond > 0 ? e.framesPerSecond : 30.0;
        const f64 seconds = static_cast<f64>(e.localTime.value) / fps;
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
    enum : u32 { kAmount = 0, kSize, kComplexity, kEvolution, kOffsetX, kOffsetY, kSeed,
                 kHorizontal, kEdges, kSpin, kMix, kPin };

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
        static const char* const pins[] = {"Nenhuma", "Todas", "Esquerda", "Direita", "Acima", "Abaixo"};
        p.add_enum("pinning", "Fixar bordas", pins, 6, 0);
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
        u.p2 = Vec4{static_cast<f32>(e.e(kEdges)), e.f(kSpin), e.f(kMix)/100.0f, static_cast<f32>(e.e(kPin))};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), finite_or(e.f(kComplexity), 3.0f), 0.0f, 0.0f};

        out = LayerImage{ctx.texture("turbulencia", w, h), region, w, h};
        if (ctx.fullscreen_pass("turbulencia", PassStage::Transform, out.texture,
                                ShaderId::effects_turbulence_displace_frag,
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
class WaveWarp final : public Effect {
public:
    enum : u32 { kHeight = 0, kWavelength, kSpeed, kPhase, kDirection, kSquare, kEdges, kPin, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kWaveWarp, "Onda", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kDirs[] = {"Horizontal", "Vertical", "Diagonal", "As duas"};
        static const char* const kEdgeModes[] = {"Repetir", "Recortar", "Esticar"};
        // Digitado ~10x o slider: uma amostra por pixel, custo fixo.
        p.add_float("height", "Altura da onda", 30.0f, 0.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 10000.0f);
        p.add_float("wavelength", "Largura de onda", 200.0f, 2.0f, 4000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(2.0f, 40000.0f);
        p.add_float("speed", "Velocidade", 0.0f, -200.0f, 200.0f, kParamAnimatable | kParamPixels, "px/quadro");
        p.typed_range(-5000.0f, 5000.0f);
        p.add_angle("phase", "Fase", 0.0f);
        p.add_enum("direction", "Direção", kDirs, 4, 0);
        p.add_bool("square", "Onda quadrada", false);
        p.add_enum("edges", "Bordas", kEdgeModes, 3, 1);
        p.add_bool("pin_edges", "Travar nas bordas", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
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
        u.p1 = Vec4{static_cast<f32>(e.e(kDirection)), e.b(kSquare) ? 1.0f : 0.0f,
                    static_cast<f32>(e.e(kEdges)), e.b(kPin) ? 1.0f : 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};
        u.p2 = Vec4{0.0f, 0.0f, input.region.x, input.region.y};   // origem da entrada (px da camada)

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
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};

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

} // namespace

void register_distort_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Shake>());
    (void)r.add(std::make_unique<Turbulence>());
    (void)r.add(std::make_unique<WaveWarp>());
    (void)r.add(std::make_unique<Warp>());
    (void)r.add(std::make_unique<RippleDissolve>());
    (void)r.add(std::make_unique<Bulge>());
}

} // namespace aurea::builtin
