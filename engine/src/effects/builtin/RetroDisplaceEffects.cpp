// =============================================================================
//  Emulador CRT · Tremor dissolvente · Mapa de deslocamento
//
//  Três efeitos escritos do zero a partir do COMPORTAMENTO descrito pelos
//  editores de movimento conhecidos (nenhum shader ou código de terceiros):
//
//   - Emulador CRT (Estilizar): a tela de tubo inteira num passe — tela
//     curva com cantos pretos, linhas de varredura, máscara de fósforo RGB
//     (grade de abertura, máscara de sombra, fenda), convergência das três
//     cores, brilho que vaza das áreas claras, vinheta, cintilação, faixa
//     rolando e chiado. Tudo o que anda é função do TEMPO da camada: toca
//     sozinho e dá o mesmo quadro no preview e no export.
//
//   - Tremor dissolvente (Glitch): a camada treme (trajetória suave do tempo)
//     e se QUEBRA em fragmentos de ruído; os que se soltam voam em direções
//     sorteadas, ficam meio transparentes ou somem. O sorteio é um hash de
//     (célula, semente, época): sem estado, determinístico pelo tempo.
//
//   - Mapa de deslocamento (Distorcer): os pixels andam conforme OUTRA camada
//     (o mapa), desenhada no mesmo instante pelo renderer
//     (`EffectBuildContext::layer_input`). Canal do mapa por eixo, deslocamento
//     máximo com sinal (valor 1 = +máx, 0,5 = parado, 0 = −máx; com máximo
//     positivo, o branco empurra a imagem para a direita/para baixo), mapa
//     centralizado/esticado/repetido e borda repetida/envolvida. Sem camada,
//     o mapa é a própria camada.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 fin(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
f32 clampf(f32 v, f32 lo, f32 hi, f32 fallback) noexcept { return std::clamp(fin(v, fallback), lo, hi); }

f64 seconds_of(const EffectEval& e) noexcept {
    const f64 fps = e.framesPerSecond > 0 ? e.framesPerSecond : 30.0;
    return static_cast<f64>(e.localTime.value) / fps;
}

Vec2 layer_size(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight)
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    return Vec2{std::max(1.0f, input.region.x + input.region.w), std::max(1.0f, input.region.y + input.region.h)};
}

// Ruído de valor 1D suave em [-1, 1] (hash inteiro, sem estado).
f32 hash01(u32 x) noexcept {
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return static_cast<f32>(x & 0xFFFFFFu) / 16777216.0f;
}
f32 smooth_noise(f64 t, u32 salt) noexcept {
    const f64 i = std::floor(t);
    const f32 f = static_cast<f32>(t - i);
    const u32 k = static_cast<u32>(static_cast<i64>(i) & 0xFFFFFFFF);
    const f32 a = hash01(k * 2654435761u ^ salt), b = hash01((k + 1u) * 2654435761u ^ salt);
    const f32 u = f * f * (3.0f - 2.0f * f);
    return (a + (b - a) * u) * 2.0f - 1.0f;
}

// =============================================================================
// Emulador CRT
// =============================================================================
class CrtEmulator final : public Effect {
public:
    enum : u32 { kCurvature = 0, kScanIntensity, kScanDensity, kMaskType, kMaskIntensity, kMaskSize, kVignette,
                 kConvergence, kBloom, kBloomRadius, kFlicker, kRollingBar, kRollSpeed, kNoise, kBrightness,
                 kContrast, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kCrtEmulator, "Emulador CRT", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_crt_emulator_frag, work));
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kMasks[] = {"Nenhuma", "Grade de abertura", "Máscara de sombra", "Fenda"};
        p.add_float("curvature", "Curvatura", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("scanline_intensity", "Intensidade das linhas", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        // Linhas na altura da camada (480 = a TV de definição padrão): a
        // densidade não depende da resolução de trabalho.
        p.add_float("scanline_density", "Densidade das linhas", 480.0f, 40.0f, 1500.0f, kParamAnimatable, "linhas");
        p.typed_range(8.0f, 4000.0f);
        p.add_enum("mask_type", "Máscara de fósforo", kMasks, 4, 1);
        p.add_float("mask_intensity", "Intensidade da máscara", 40.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("mask_size", "Tamanho da máscara", 2.0f, 1.0f, 12.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.5f, 100.0f);
        p.add_float("vignette", "Vinheta", 35.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("convergence", "Convergência", 1.5f, -20.0f, 20.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(-200.0f, 200.0f);
        p.add_float("bloom", "Brilho", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("bloom_radius", "Raio do brilho", 6.0f, 0.0f, 50.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 500.0f);
        p.add_float("flicker", "Cintilação", 10.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("rolling_bar", "Faixa rolando", 20.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("roll_speed", "Velocidade da faixa", 0.25f, -5.0f, 5.0f, kParamAnimatable, "telas/s");
        p.typed_range(-60.0f, 60.0f);
        p.add_float("noise", "Ruído", 8.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("brightness", "Brilho geral", 10.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("contrast", "Contraste", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(fin(e.f(kMix), 100) > 0.01f); }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::fabs(fin(e.f(kConvergence), 0)) + clampf(e.f(kBloomRadius), 0, 500, 0);
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        struct {
            Vec4 uvMap, texel, region, screen, a, b, c, d, f;
        } u{};
        const EffectUniforms base = base_uniforms(input);
        u.uvMap = base.uvMap;
        u.texel = base.texel;
        u.region = Vec4{input.region.x, input.region.y, std::max(input.region.w, 1e-3f), std::max(input.region.h, 1e-3f)};
        const Vec2 ls = layer_size(e, input);
        const f64 secs = seconds_of(e);
        u.screen = Vec4{ls.x, ls.y, static_cast<f32>(std::fmod(secs, 3600.0)), static_cast<f32>(e.localTime.value & 0xFFFFFF)};
        u.a = Vec4{clampf(e.f(kCurvature), 0, 100, 25) / 100.0f, clampf(e.f(kScanIntensity), 0, 100, 50) / 100.0f,
                   clampf(e.f(kScanDensity), 8, 4000, 480), static_cast<f32>(std::min<u32>(e.e(kMaskType), 3u))};
        u.b = Vec4{clampf(e.f(kMaskIntensity), 0, 100, 40) / 100.0f, clampf(e.f(kMaskSize), 0.5f, 100, 2),
                   clampf(e.f(kVignette), 0, 100, 35) / 100.0f, clampf(e.f(kConvergence), -200, 200, 1.5f)};
        u.c = Vec4{clampf(e.f(kBloom), 0, 100, 30) / 100.0f, clampf(e.f(kBloomRadius), 0, 500, 6),
                   clampf(e.f(kFlicker), 0, 100, 10) / 100.0f, clampf(e.f(kRollingBar), 0, 100, 20) / 100.0f};
        // Posição da faixa já resolvida aqui (em f64): o shader recebe 0..1.
        const f64 roll = static_cast<f64>(clampf(e.f(kRollSpeed), -60, 60, 0.25f)) * secs;
        u.d = Vec4{static_cast<f32>(roll - std::floor(roll)), clampf(e.f(kNoise), 0, 100, 8) / 100.0f,
                   clampf(e.f(kBrightness), -100, 100, 10) / 100.0f, clampf(e.f(kContrast), -100, 100, 0) / 100.0f};
        u.f = Vec4{clampf(e.f(kMix), 0, 100, 100) / 100.0f, 0, 0, 0};
        out = input;
        out.history = FGTexture{};
        out.texture = ctx.texture("emulador-crt", input.width, input.height);
        return ctx.fullscreen_pass("emulador-crt", PassStage::Effects, out.texture, ShaderId::effects_crt_emulator_frag,
                                   {PassTexture{input.texture, {}, CommonSampler::LinearClamp}}, &u, sizeof(u)) == kInvalidIndex
            ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};

// =============================================================================
// Tremor dissolvente
// =============================================================================
class DissolveShake final : public Effect {
public:
    enum : u32 { kAmplitude = 0, kFrequency, kDissolve, kGrainSize, kScatter, kRandomness, kFragmentOpacity,
                 kEvolution, kAxes, kSeed, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kDissolveShake, "Tremor dissolvente", "Glitch", EffectClass::Domain};
        return i;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_dissolve_shake_frag, work));
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kAxesNames[] = {"As duas", "Horizontal", "Vertical"};
        p.add_float("amplitude", "Amplitude", 25.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_float("frequency", "Frequência", 8.0f, 0.0f, 30.0f, kParamAnimatable, "Hz");
        p.typed_range(0.0f, 240.0f);
        p.add_float("dissolve", "Dissolução", 35.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("grain_size", "Tamanho do fragmento", 12.0f, 1.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(1.0f, 2000.0f);
        p.add_float("scatter", "Dispersão", 40.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_float("randomness", "Aleatoriedade", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("fragment_opacity", "Transparência dos fragmentos", 50.0f, 0.0f, 100.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.add_float("evolution", "Velocidade da evolução", 1.0f, 0.0f, 10.0f, kParamAnimatable, "x");
        p.typed_range(0.0f, 100.0f);
        p.add_enum("axes", "Eixos", kAxesNames, 3, 0);
        p.add_int("seed", "Semente", 1, 0, 9999);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (!(fin(e.f(kMix), 100) > 0.01f)) return true;
        const bool still = !(fin(e.f(kAmplitude), 0) > 0.01f) || !(fin(e.f(kFrequency), 0) > 0.0f);
        return still && !(fin(e.f(kDissolve), 0) > 0.01f);
    }
    static f32 reach(const EffectEval& e) noexcept {
        const f32 amp = clampf(e.f(kAmplitude), 0, 5000, 0);
        const f32 rnd = clampf(e.f(kRandomness), 0, 100, 0) / 100.0f;
        return amp * (1.0f + rnd) + clampf(e.f(kScatter), 0, 5000, 0);
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return reach(e); }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 r = reach(e);
        const Rect region = spread_region(input.region, r, r, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        const f64 secs = seconds_of(e);
        const f32 amp = clampf(e.f(kAmplitude), 0, 5000, 25);
        const f32 freq = clampf(e.f(kFrequency), 0, 240, 8);
        const u32 seed = std::min<u32>(e.e(kSeed), 9999u);
        const u32 axes = std::min<u32>(e.e(kAxes), 2u);
        // Trajetória suave: duas oitavas de ruído de valor por eixo.
        const f64 t = secs * static_cast<f64>(freq);
        Vec2 shake{0.0f, 0.0f};
        if (freq > 0.0f) {
            shake.x = amp * (0.75f * smooth_noise(t, seed * 0x9E3779B9u + 11u) + 0.25f * smooth_noise(t * 2.3, seed * 0x85EBCA6Bu + 5u));
            shake.y = amp * (0.75f * smooth_noise(t, seed * 0xC2B2AE35u + 23u) + 0.25f * smooth_noise(t * 2.3, seed * 0x27D4EB2Fu + 7u));
        }
        if (axes == 1) shake.y = 0.0f;
        if (axes == 2) shake.x = 0.0f;
        // Época dos fragmentos: 8 sorteios por segundo na velocidade 1x (0 = parado).
        const f64 epoch = std::floor(secs * static_cast<f64>(clampf(e.f(kEvolution), 0, 100, 1)) * 8.0);

        struct {
            Vec4 uvMap, texel, outRegion, inRegion, a, b, c;
        } u{};
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{1.0f / static_cast<f32>(input.width), 1.0f / static_cast<f32>(input.height),
                       input.texel_scale_x(), input.texel_scale_y()};
        u.outRegion = Vec4{region.x, region.y, std::max(region.w, 1e-3f), std::max(region.h, 1e-3f)};
        u.inRegion = Vec4{input.region.x, input.region.y, std::max(input.region.w, 1e-3f), std::max(input.region.h, 1e-3f)};
        u.a = Vec4{shake.x, shake.y, clampf(e.f(kGrainSize), 1, 2000, 12), clampf(e.f(kScatter), 0, 5000, 40)};
        u.b = Vec4{clampf(e.f(kDissolve), 0, 100, 35) / 100.0f, clampf(e.f(kRandomness), 0, 100, 60) / 100.0f,
                   clampf(e.f(kFragmentOpacity), 0, 100, 50) / 100.0f, static_cast<f32>(std::fmod(epoch, 65536.0))};
        u.c = Vec4{static_cast<f32>(seed), static_cast<f32>(axes), clampf(e.f(kMix), 0, 100, 100) / 100.0f, amp};

        out = LayerImage{ctx.texture("tremor-dissolvente", w, h), region, w, h};
        return ctx.fullscreen_pass("tremor-dissolvente", PassStage::Transform, out.texture,
                                   ShaderId::effects_dissolve_shake_frag,
                                   {PassTexture{input.texture, {}, CommonSampler::LinearClamp}}, &u, sizeof(u)) == kInvalidIndex
            ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};

// =============================================================================
// Mapa de deslocamento
// =============================================================================
class DisplacementMap final : public Effect {
public:
    enum : u32 { kMapLayer = 0, kHorizontalChannel, kVerticalChannel, kMaxHorizontal, kMaxVertical, kBehavior,
                 kEdges, kExpand, kMix };
    /// Canais (contrato com displacement_map.frag).
    enum : u32 { kRed = 0, kGreen, kBlue, kLuminance, kAlpha, kOff };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kDisplacementMap, "Mapa de deslocamento", "Distorcer", EffectClass::Domain};
        return i;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_displacement_map_frag, work));
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kChannels[] = {"Vermelho", "Verde", "Azul", "Luminância", "Alfa", "Desligado"};
        static const char* const kBehaviors[] = {"Centralizar", "Esticar", "Repetir"};
        static const char* const kEdgeModes[] = {"Repetir pixels", "Envolver"};
        p.add_layer_ref("map_layer", "Camada do mapa");
        p.add_enum("horizontal_channel", "Canal horizontal", kChannels, 6, kRed);
        p.add_enum("vertical_channel", "Canal vertical", kChannels, 6, kGreen);
        p.add_float("max_horizontal", "Deslocamento horizontal máx.", 20.0f, -500.0f, 500.0f,
                    kParamAnimatable | kParamPixels, "px");
        p.typed_range(-10000.0f, 10000.0f);
        p.add_float("max_vertical", "Deslocamento vertical máx.", 20.0f, -500.0f, 500.0f,
                    kParamAnimatable | kParamPixels, "px");
        p.typed_range(-10000.0f, 10000.0f);
        p.add_enum("map_behavior", "Comportamento do mapa", kBehaviors, 3, 1);
        p.add_enum("edge_behavior", "Bordas", kEdgeModes, 2, 0);
        p.add_bool("expand_output", "Expandir saída", true);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kMapLayer); }
    static Vec2 amounts(const EffectEval& e) noexcept {
        const f32 h = e.e(kHorizontalChannel) >= kOff ? 0.0f : clampf(e.f(kMaxHorizontal), -10000, 10000, 0);
        const f32 v = e.e(kVerticalChannel) >= kOff ? 0.0f : clampf(e.f(kMaxVertical), -10000, 10000, 0);
        return Vec2{h, v};
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        const Vec2 a = amounts(e);
        return !(fin(e.f(kMix), 100) > 0.01f) || (std::fabs(a.x) < 1e-3f && std::fabs(a.y) < 1e-3f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        const Vec2 a = amounts(e);
        return std::max(std::fabs(a.x), std::fabs(a.y));
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHorizontalChannel] = ParamValue::scalar(static_cast<f32>(kLuminance));
        v[kVerticalChannel] = ParamValue::scalar(static_cast<f32>(kLuminance));
        v[kMaxHorizontal] = ParamValue::scalar(30.0f);
        v[kMaxVertical] = ParamValue::scalar(30.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const u64 ref = e.value(kMapLayer).ref;
        const LayerImage* map = ref ? ctx.layer_input(ref) : nullptr;
        if (ref && !map) {
            // Camada escolhida que não aparece neste instante (fora do tempo,
            // apagada, invisível por opacidade 0): nada a deslocar.
            out = input;
            return OkStatus;
        }
        const Vec2 a = amounts(e);
        const bool expand = e.b(kExpand);
        const Rect region = expand ? spread_region(input.region, std::fabs(a.x), std::fabs(a.y), e.placement, margin)
                                   : input.region;
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        const Vec2 ls = layer_size(e, input);

        struct {
            Vec4 uvMap, texel, outRegion, inRegion, a, b, c;
        } u{};
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{1.0f / static_cast<f32>(input.width), 1.0f / static_cast<f32>(input.height),
                       input.texel_scale_x(), input.texel_scale_y()};
        u.outRegion = Vec4{region.x, region.y, std::max(region.w, 1e-3f), std::max(region.h, 1e-3f)};
        u.inRegion = Vec4{input.region.x, input.region.y, std::max(input.region.w, 1e-3f), std::max(input.region.h, 1e-3f)};
        u.a = Vec4{a.x, a.y, static_cast<f32>(std::min<u32>(e.e(kHorizontalChannel), kOff)),
                   static_cast<f32>(std::min<u32>(e.e(kVerticalChannel), kOff))};
        u.b = Vec4{static_cast<f32>(std::min<u32>(e.e(kBehavior), 2u)), static_cast<f32>(std::min<u32>(e.e(kEdges), 1u)),
                   clampf(e.f(kMix), 0, 100, 100) / 100.0f, map ? 1.0f : 0.0f};
        const f32 mapW = map ? std::max(map->region.w, 1.0f) : ls.x;
        const f32 mapH = map ? std::max(map->region.h, 1.0f) : ls.y;
        u.c = Vec4{ls.x, ls.y, mapW, mapH};

        out = LayerImage{ctx.texture("mapa-de-deslocamento", w, h), region, w, h};
        const FGTexture mapTex = map ? map->texture : input.texture;
        return ctx.fullscreen_pass("mapa-de-deslocamento", PassStage::Transform, out.texture,
                                   ShaderId::effects_displacement_map_frag,
                                   {PassTexture{input.texture, {}, CommonSampler::LinearClamp},
                                    PassTexture{mapTex, {}, CommonSampler::LinearClamp}},
                                   &u, sizeof(u)) == kInvalidIndex
            ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};

} // namespace

void register_retro_displace_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<CrtEmulator>());
    (void)r.add(std::make_unique<DissolveShake>());
    (void)r.add(std::make_unique<DisplacementMap>());
}

} // namespace aurea::builtin
