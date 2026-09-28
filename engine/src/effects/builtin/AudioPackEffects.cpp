// =============================================================================
//  Pacote de áudio — os efeitos de SOM da camada e os três visuais novos.
//
//  Áudio (categoria "Áudio"): Reverso, Atraso, Flange e chorus, Passa-alta/
//  Passa-baixa, Mixer estéreo, Modulador, EQ paramétrico, Reverb e Tom. Na
//  pilha de efeitos eles são efeitos comuns (keyframes, expressões, desfazer,
//  predefinições); no vídeo são identidade. O que eles fazem com o som mora
//  no mixer (audio/AudioFx.cpp), que lê estes mesmos parâmetros pelos índices
//  de `audio::fxp` — a declaração aqui e o DSP lá são um contrato só.
//
//  Visuais: Forma de onda de áudio e Espectro de áudio (o som de uma camada
//  escolhida, analisado pelo renderer no planejamento: EffectResources::
//  audio_analysis) e Bolas (a camada vira uma grade de esferas sombreadas).
// =============================================================================
#include "BuiltinEffects.hpp"

#include "aurea/audio/AudioEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

constexpr u16 kAnim = kParamAnimatable;
constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;

// =============================================================================
// Áudio
// =============================================================================
/// Base dos efeitos de som: no vídeo não fazem nada (saem da cadeia de pixels).
class AudioOnlyEffect : public Effect {
public:
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

class Backwards final : public AudioOnlyEffect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kBackwards, "Reverso", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_bool("swap_channels", "Trocar canais", false);
    }
};

class Delay final : public AudioOnlyEffect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kDelay, "Atraso", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("delay_time", "Tempo de atraso", 500.0f, 1.0f, 2000.0f, kAnim, "ms");
        p.typed_range(1.0f, 10000.0f);
        p.add_float("delay_amount", "Quantidade de atraso", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("feedback", "Realimentação", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("dry_out", "Saída seca", 75.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("wet_out", "Saída molhada", 75.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
};

class FlangeChorus final : public AudioOnlyEffect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kFlangeChorus, "Flange e chorus", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("voice_separation", "Separação das vozes", 3.0f, 0.0f, 50.0f, kAnim, "ms");
        p.typed_range(0.0f, 200.0f);
        p.add_int("voices", "Vozes", 1, 1, 8);
        p.add_float("rate", "Taxa de modulação", 0.8f, 0.0f, 10.0f, kAnim, "Hz");
        p.typed_range(0.0f, 50.0f);
        p.add_float("depth", "Profundidade de modulação", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("voice_phase", "Mudança de fase das vozes", 0.0f, 0.0f, 360.0f, kAnim, "°");
        p.add_bool("invert_phase", "Inverter fase", false);
        p.add_bool("stereo_voices", "Vozes estéreo", false);
        p.add_float("dry_out", "Saída seca", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("wet_out", "Saída molhada", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
};

class HighLowPass final : public AudioOnlyEffect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kHighLowPass, "Passa-alta/Passa-baixa", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kTypes[] = {"Passa-alta", "Passa-baixa"};
        p.add_enum("filter", "Opções do filtro", kTypes, 2, 0);
        p.add_float("cutoff", "Frequência de corte", 2000.0f, 20.0f, 20000.0f, kAnim, "Hz");
        p.add_float("dry_out", "Saída seca", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("wet_out", "Saída molhada", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
};

class StereoMixer final : public AudioOnlyEffect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kStereoMixer, "Mixer estéreo", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("left_level", "Nível esquerdo", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.add_float("right_level", "Nível direito", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.add_float("left_pan", "Pan esquerdo", -100.0f, -100.0f, 100.0f, kAnim);
        p.add_float("right_pan", "Pan direito", 100.0f, -100.0f, 100.0f, kAnim);
        p.add_bool("invert_phase", "Inverter fase", false);
    }
};

class Modulator final : public AudioOnlyEffect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kModulator, "Modulador", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kTypes[] = {"Seno", "Triângulo"};
        p.add_enum("type", "Tipo de modulação", kTypes, 2, 0);
        p.add_float("rate", "Taxa de modulação", 1.0f, 0.1f, 20.0f, kAnim, "Hz");
        p.typed_range(0.01f, 100.0f);
        p.add_float("depth", "Profundidade de modulação", 2.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("amplitude", "Modulação de amplitude", 25.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
};

class ParametricEq final : public AudioOnlyEffect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kParametricEq, "EQ paramétrico", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kEnable[] = {"Banda 1 ativada", "Banda 2 ativada", "Banda 3 ativada"};
        static const char* const kFreq[] = {"Frequência 1", "Frequência 2", "Frequência 3"};
        static const char* const kWidth[] = {"Largura de banda 1", "Largura de banda 2", "Largura de banda 3"};
        static const char* const kGain[] = {"Reforço/Corte 1", "Reforço/Corte 2", "Reforço/Corte 3"};
        static const char* const kEnableId[] = {"band1_enable", "band2_enable", "band3_enable"};
        static const char* const kFreqId[] = {"band1_frequency", "band2_frequency", "band3_frequency"};
        static const char* const kWidthId[] = {"band1_bandwidth", "band2_bandwidth", "band3_bandwidth"};
        static const char* const kGainId[] = {"band1_gain", "band2_gain", "band3_gain"};
        constexpr f32 kHz[] = {250.0f, 1000.0f, 4000.0f};
        for (u32 b = 0; b < 3; ++b) {
            p.add_bool(kEnableId[b], kEnable[b], b == 0);
            p.add_float(kFreqId[b], kFreq[b], kHz[b], 20.0f, 20000.0f, kAnim, "Hz");
            p.add_float(kWidthId[b], kWidth[b], 30.0f, 1.0f, 100.0f, kAnimPct, "%");
            p.typed_range(0.5f, 400.0f);
            p.add_float(kGainId[b], kGain[b], 0.0f, -20.0f, 20.0f, kAnim, "dB");
            p.typed_range(-40.0f, 40.0f);
        }
    }
};

class RoomReverb final : public AudioOnlyEffect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kReverb, "Reverb", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("reverb_time", "Tempo de reverberação", 100.0f, 1.0f, 1000.0f, kAnim, "ms");
        p.typed_range(1.0f, 2000.0f);
        p.add_float("diffusion", "Difusão", 75.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("decay", "Decaimento", 25.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("brightness", "Brilho", 10.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("dry_out", "Saída seca", 90.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("wet_out", "Saída molhada", 10.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
};

class Tone final : public AudioOnlyEffect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kTone, "Tom", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kShapes[] = {"Seno", "Triângulo", "Dente de serra", "Quadrada"};
        static const char* const kIds[] = {"frequency1", "frequency2", "frequency3", "frequency4", "frequency5"};
        static const char* const kLabels[] = {"Frequência 1", "Frequência 2", "Frequência 3", "Frequência 4", "Frequência 5"};
        constexpr f32 kHz[] = {440.0f, 493.88f, 587.33f, 659.26f, 783.99f};
        p.add_enum("waveform", "Forma de onda", kShapes, 4, 0);
        for (u32 k = 0; k < 5; ++k) {
            p.add_float(kIds[k], kLabels[k], kHz[k], 0.0f, 2000.0f, kAnim, "Hz");
            p.typed_range(0.0f, 20000.0f);
        }
        p.add_float("level", "Nível", 20.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
};

// =============================================================================
// Visuais
// =============================================================================
Vec2 layer_size(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight) {
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    }
    return Vec2{input.region.w, input.region.h};
}

Vec2 layer_point(const EffectEval& e, const LayerImage& input, Vec2 rel, Vec2 fallback) noexcept {
    const Vec2 size = layer_size(e, input);
    return Vec2{finite_or(rel.x, fallback.x) * size.x, finite_or(rel.y, fallback.y) * size.y};
}

struct VisualUniforms {
    EffectUniforms base{};
    Vec4 q0{};
    Vec4 q1{};
    Vec4 q2{};
    Vec4 q3{};
};
static_assert(sizeof(VisualUniforms) == 176, "layout std140 dos visuais de áudio");

Status analysis_pass(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, ShaderId frag,
                     const VisualUniforms& u, const char* name, LayerImage& out) {
    out = input;
    out.texture = ctx.texture(name, input.width, input.height);
    // Sem som (ou sem GPU), o slot recebe a própria entrada e o shader ignora:
    // o desenho repousa numa linha reta.
    const PassTexture data = e.aux.valid() ? PassTexture{{}, e.aux, CommonSampler::NearestClamp}
                                           : PassTexture{input.texture, {}, CommonSampler::NearestClamp};
    if (ctx.fullscreen_pass(name, PassStage::Effects, out.texture, frag,
                            {PassTexture{input.texture, {}, CommonSampler::LinearClamp}, data}, &u, sizeof(u))
        == kInvalidIndex) {
        return Errc::PipelineCompileFailed;
    }
    return OkStatus;
}

class AudioWaveform final : public Effect {
public:
    enum : u32 { kLayer = 0, kStart, kEnd, kSamples, kHeight, kDuration, kOffset, kThickness, kSoftness, kSeed,
                 kInside, kOutside, kChannel, kDisplay, kComposite };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kAudioWaveform, "Forma de onda de áudio", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kChannels[] = {"Mono", "Esquerdo", "Direito"};
        static const char* const kDisplays[] = {"Digital", "Linhas analógicas", "Pontos analógicos"};
        p.add_layer_ref("audio_layer", "Camada de áudio");
        p.add_point2("start", "Ponto inicial", Vec2{0.1f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("end", "Ponto final", Vec2{0.9f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_int("samples", "Amostras exibidas", 200, 2, 1024);
        p.add_float("height", "Altura máxima", 300.0f, 0.0f, 2000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 20000.0f);
        p.add_float("duration", "Duração do áudio", 200.0f, 1.0f, 1000.0f, kAnim, "ms");
        p.typed_range(1.0f, 2000.0f);
        p.add_float("offset", "Deslocamento do áudio", 0.0f, -1000.0f, 1000.0f, kAnim, "ms");
        p.typed_range(-60000.0f, 60000.0f);
        p.add_float("thickness", "Espessura", 2.0f, 0.0f, 50.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 500.0f);
        p.add_float("softness", "Suavidade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_int("seed", "Semente aleatória (analógico)", 1, 0, 1000);
        p.add_color("inside_color", "Cor interna", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_color("outside_color", "Cor externa", Vec4{0.10f, 0.45f, 1.0f, 1.0f});
        p.add_enum("channel", "Opções de forma de onda", kChannels, 3, 0);
        p.add_enum("display", "Opções de exibição", kDisplays, 3, 1);
        p.add_bool("composite", "Compor sobre o original", false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // A prévia não tem som: a linha em repouso sobre a foto mostra onde
        // a forma de onda desenha.
        v[kThickness] = ParamValue::scalar(8.0f);
        v[kComposite] = ParamValue::boolean(true);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_audio_waveform_frag, work));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{};
        if (!e.resources) return;
        AudioAnalysisRequest req;
        req.host = e.layer;
        req.instance = e.instance;
        req.layer = e.value(kLayer).ref;
        req.spectrum = false;
        req.count = static_cast<u32>(std::clamp(e.value(kSamples).as_int(), 2, 1024));
        req.durationMs = std::clamp(finite_or(e.f(kDuration), 200.0f), 1.0f, 2000.0f);
        req.offsetMs = std::clamp(finite_or(e.f(kOffset), 0.0f), -60000.0f, 60000.0f);
        req.channel = std::min<u32>(e.e(kChannel), 2u);
        const AudioAnalysisResult r = e.resources->audio_analysis(req);
        e.aux = r.texture;
        e.auxInfo = Vec4{r.texture.valid() ? 1.0f : 0.0f, r.peak, 0.0f, 0.0f};
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 a = layer_point(e, input, e.p2(kStart), Vec2{0.1f, 0.5f});
        const Vec2 b = layer_point(e, input, e.p2(kEnd), Vec2{0.9f, 0.5f});
        VisualUniforms u;
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{a.x, a.y, b.x, b.y};
        u.base.p1 = Vec4{std::clamp(finite_or(e.f(kHeight), 300.0f), 0.0f, 20000.0f),
                         std::clamp(finite_or(e.f(kThickness), 2.0f), 0.0f, 500.0f),
                         std::clamp(finite_or(e.f(kSoftness), 50.0f) / 100.0f, 0.0f, 1.0f),
                         static_cast<f32>(std::clamp(e.value(kSamples).as_int(), 2, 1024))};
        u.base.p2 = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        u.base.p3 = Vec4{static_cast<f32>(std::min<u32>(e.e(kDisplay), 2u)), e.b(kComposite) ? 1.0f : 0.0f,
                         e.aux.valid() ? 1.0f : 0.0f, static_cast<f32>(std::max(0, e.value(kSeed).as_int()))};
        u.base.color = e.color(kInside);
        u.q0 = e.color(kOutside);
        return analysis_pass(ctx, e, input, ShaderId::effects_audio_waveform_frag, u, "forma-de-onda", out);
    }
};

class AudioSpectrumAnalyzer final : public Effect {
public:
    enum : u32 { kLayer = 0, kStart, kEnd, kPolar, kStartHz, kEndHz, kBands, kHeight, kDuration, kOffset, kThickness,
                 kSoftness, kInside, kOutside, kBlend, kHueInterp, kDynamicHue, kSymmetry, kDisplay, kSide,
                 kAveraging, kComposite };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kAudioSpectrumAnalyzer, "Espectro de áudio", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kDisplays[] = {"Digital", "Linhas analógicas", "Pontos analógicos"};
        static const char* const kSides[] = {"Lado A", "Lado B", "Lados A e B"};
        p.add_layer_ref("audio_layer", "Camada de áudio");
        p.add_point2("start", "Ponto inicial", Vec2{0.1f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("end", "Ponto final", Vec2{0.9f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_bool("polar", "Usar caminho polar", false);
        p.add_float("start_frequency", "Frequência inicial", 20.0f, 1.0f, 20000.0f, kAnim, "Hz");
        p.add_float("end_frequency", "Frequência final", 1000.0f, 1.0f, 20000.0f, kAnim, "Hz");
        p.add_int("bands", "Bandas de frequência", 64, 1, 256);
        p.add_float("height", "Altura máxima", 1000.0f, 0.0f, 5000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 50000.0f);
        p.add_float("duration", "Duração do áudio", 90.0f, 1.0f, 1000.0f, kAnim, "ms");
        p.typed_range(1.0f, 2000.0f);
        p.add_float("offset", "Deslocamento do áudio", 0.0f, -1000.0f, 1000.0f, kAnim, "ms");
        p.typed_range(-60000.0f, 60000.0f);
        p.add_float("thickness", "Espessura", 3.0f, 0.0f, 50.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 500.0f);
        p.add_float("softness", "Suavidade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_color("inside_color", "Cor interna", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_color("outside_color", "Cor externa", Vec4{0.10f, 0.45f, 1.0f, 1.0f});
        p.add_bool("blend_overlap", "Misturar cores sobrepostas", true);
        p.add_angle("hue_interpolation", "Interpolação de matiz", 0.0f);
        p.add_bool("dynamic_hue", "Matiz dinâmica", false);
        p.add_bool("color_symmetry", "Simetria de cor", false);
        p.add_enum("display", "Opções de exibição", kDisplays, 3, 0);
        p.add_enum("side", "Opções de lado", kSides, 3, 2);
        p.add_bool("duration_averaging", "Média de duração", false);
        p.add_bool("composite", "Compor sobre o original", false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kDisplay] = ParamValue::scalar(1.0f);
        v[kThickness] = ParamValue::scalar(8.0f);
        v[kComposite] = ParamValue::boolean(true);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_spectrum_analyzer_frag, work));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{};
        if (!e.resources) return;
        AudioAnalysisRequest req;
        req.host = e.layer;
        req.instance = e.instance;
        req.layer = e.value(kLayer).ref;
        req.spectrum = true;
        req.count = static_cast<u32>(std::clamp(e.value(kBands).as_int(), 1, 256));
        req.durationMs = std::clamp(finite_or(e.f(kDuration), 90.0f), 1.0f, 2000.0f);
        req.offsetMs = std::clamp(finite_or(e.f(kOffset), 0.0f), -60000.0f, 60000.0f);
        req.startHz = std::clamp(finite_or(e.f(kStartHz), 20.0f), 1.0f, 24000.0f);
        req.endHz = std::clamp(finite_or(e.f(kEndHz), 1000.0f), 1.0f, 24000.0f);
        req.averaging = e.b(kAveraging);
        const AudioAnalysisResult r = e.resources->audio_analysis(req);
        e.aux = r.texture;
        e.auxInfo = Vec4{r.texture.valid() ? 1.0f : 0.0f, r.peak, 0.0f, 0.0f};
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 a = layer_point(e, input, e.p2(kStart), Vec2{0.1f, 0.5f});
        const Vec2 b = layer_point(e, input, e.p2(kEnd), Vec2{0.9f, 0.5f});
        VisualUniforms u;
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{a.x, a.y, b.x, b.y};
        u.base.p1 = Vec4{std::clamp(finite_or(e.f(kHeight), 1000.0f), 0.0f, 50000.0f),
                         std::clamp(finite_or(e.f(kThickness), 3.0f), 0.0f, 500.0f),
                         std::clamp(finite_or(e.f(kSoftness), 50.0f) / 100.0f, 0.0f, 1.0f),
                         static_cast<f32>(std::clamp(e.value(kBands).as_int(), 1, 256))};
        u.base.p2 = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        u.base.p3 = Vec4{static_cast<f32>(std::min<u32>(e.e(kDisplay), 2u)), static_cast<f32>(std::min<u32>(e.e(kSide), 2u)),
                         e.b(kPolar) ? 1.0f : 0.0f, e.b(kComposite) ? 1.0f : 0.0f};
        u.base.color = e.color(kInside);
        u.q0 = e.color(kOutside);
        u.q1 = Vec4{finite_or(e.f(kHueInterp), 0.0f) / 360.0f, e.b(kDynamicHue) ? e.auxInfo.y : 0.0f,
                    e.b(kSymmetry) ? 1.0f : 0.0f, e.b(kBlend) ? 1.0f : 0.0f};
        u.q2 = Vec4{e.aux.valid() ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        return analysis_pass(ctx, e, input, ShaderId::effects_spectrum_analyzer_frag, u, "espectro-bandas", out);
    }
};

// -----------------------------------------------------------------------------
// Bolas: cada célula da grade vira uma esfera com a cor da camada ali. A grade
// gira (eixo e ângulo), torce (um ângulo a mais que varia com a propriedade
// escolhida), espalha (deslocamento aleatório fixo por bola) e treme
// (instabilidade: um vaivém por bola, fase pelo "estado"). Perspectiva de uma
// câmera à frente da camada; profundidade real entre as bolas.
// -----------------------------------------------------------------------------
class BallGrid final : public Effect {
public:
    enum : u32 { kScatter = 0, kAxis, kRotation, kTwistProperty, kTwistAngle, kSpacing, kBallSize,
                 kInstabilityState, kInstability };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kBallGrid, "Bolas", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kAxes[] = {"X", "Y", "Z", "XY", "XZ", "YZ", "XYZ"};
        static const char* const kTwist[] = {"Eixo X", "Eixo Y", "Centro X", "Centro Y", "Radial", "Canto",
                                             "Vermelho", "Verde", "Azul", "Brilho", "Alfa"};
        p.add_float("scatter", "Dispersão", 0.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_enum("rotation_axis", "Eixo de rotação", kAxes, 7, 0);
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_enum("twist_property", "Propriedade da torção", kTwist, 11, 0);
        p.add_angle("twist_angle", "Ângulo de torção", 0.0f);
        p.add_float("grid_spacing", "Espaçamento da grade", 8.0f, 1.0f, 100.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(1.0f, 400.0f);
        p.add_float("ball_size", "Tamanho da bola", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.0f, 400.0f);
        p.add_angle("instability_state", "Estado de instabilidade", 0.0f);
        p.add_float("instability", "Instabilidade", 0.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 2000.0f);
    }
    bool is_identity(const EffectEval&) const noexcept override { return false; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        PipelineKey k = PipelineKey::graphics(ShaderId::effects_ball_grid_vert, ShaderId::effects_ball_grid_frag, work);
        k.hasDepth = true;
        k.depthTest = true;
        k.depthWrite = true;
        k.depthCompare = CompareOp::GreaterOrEqual;
        k.depthFormat = SurfaceFormat::Depth32F;
        out.push_back(k);
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        struct BallUniforms {
            Vec4 grid;     // colunas, linhas, espaçamento (px), raio (px)
            Vec4 motion;   // dispersão (px), rotação (rad), torção (rad), propriedade da torção
            Vec4 region;   // região da imagem (px da camada)
            Vec4 extra;    // eixo, estado (rad), instabilidade (px), distância focal (px)
            Vec4 center;   // centro da rotação (px), tamanho natural da camada
            Vec4 texel;    // texels por px da camada (x, y), px por texel
        } u{};
        static_assert(sizeof(BallUniforms) == 96);
        const Rect r = input.region;
        f32 spacing = std::clamp(finite_or(e.f(kSpacing), 8.0f), 1.0f, 400.0f);
        // Teto de bolas (vértices = 6 por bola): o espaçamento cresce antes.
        constexpr f32 kMaxBalls = 262144.0f;
        while (std::ceil(r.w / spacing) * std::ceil(r.h / spacing) > kMaxBalls) spacing *= 1.25f;
        const f32 cols = std::max(1.0f, std::ceil(r.w / spacing));
        const f32 rows = std::max(1.0f, std::ceil(r.h / spacing));
        const f32 radius = 0.5f * spacing * std::clamp(finite_or(e.f(kBallSize), 100.0f), 0.0f, 400.0f) / 100.0f;
        const f32 deg = 0.01745329252f;
        Vec2 size{r.w, r.h};
        if (e.placement && e.placement->layerWidth && e.placement->layerHeight) {
            size = Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
        }
        u.grid = Vec4{cols, rows, spacing, radius};
        u.motion = Vec4{std::clamp(finite_or(e.f(kScatter), 0.0f), 0.0f, 5000.0f), finite_or(e.f(kRotation), 0.0f) * deg,
                        finite_or(e.f(kTwistAngle), 0.0f) * deg, static_cast<f32>(std::min<u32>(e.e(kTwistProperty), 10u))};
        u.region = Vec4{r.x, r.y, r.w, r.h};
        u.extra = Vec4{static_cast<f32>(std::min<u32>(e.e(kAxis), 6u)), finite_or(e.f(kInstabilityState), 0.0f) * deg,
                       std::clamp(finite_or(e.f(kInstability), 0.0f), 0.0f, 2000.0f),
                       2.0f * std::max(size.x, size.y)};
        u.center = Vec4{size.x * 0.5f, size.y * 0.5f, size.x, size.y};
        u.texel = Vec4{input.texel_scale_x(), input.texel_scale_y(), 1.0f / std::max(input.texel_scale_x(), 1e-4f), 0.0f};
        out = input;
        out.texture = ctx.texture("bolas", input.width, input.height);
        const u32 vertices = static_cast<u32>(cols * rows) * 6u;
        if (radius <= 0.0f) {
            // Bola de tamanho zero: nada a desenhar (a camada some).
            if (ctx.geometry_pass("bolas", PassStage::Effects, out.texture, ShaderId::effects_ball_grid_vert,
                                  ShaderId::effects_ball_grid_frag, {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                  &u, sizeof(u), 0, true) == kInvalidIndex) {
                return Errc::PipelineCompileFailed;
            }
            return OkStatus;
        }
        if (ctx.geometry_pass("bolas", PassStage::Effects, out.texture, ShaderId::effects_ball_grid_vert,
                              ShaderId::effects_ball_grid_frag, {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                              &u, sizeof(u), vertices, true) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_audio_pack_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Backwards>());
    (void)r.add(std::make_unique<Delay>());
    (void)r.add(std::make_unique<FlangeChorus>());
    (void)r.add(std::make_unique<HighLowPass>());
    (void)r.add(std::make_unique<StereoMixer>());
    (void)r.add(std::make_unique<Modulator>());
    (void)r.add(std::make_unique<ParametricEq>());
    (void)r.add(std::make_unique<RoomReverb>());
    (void)r.add(std::make_unique<Tone>());
    (void)r.add(std::make_unique<AudioWaveform>());
    (void)r.add(std::make_unique<AudioSpectrumAnalyzer>());
    (void)r.add(std::make_unique<BallGrid>());
}

} // namespace aurea::builtin
