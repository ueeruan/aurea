// =============================================================================
//  Aurea / effects / builtin / ColorGradeEffect.cpp
//
//  Correção de cor (Cor, 0.3.0): o corretor de cor completo num efeito só, na
//  ordem de um corretor profissional (shaders/effects/color_grade.frag):
//   Entrada   — perfil log da câmera (S-Log3, LogC3, V-Log, Apple Log, Cineon)
//               convertido para Rec.709 (fórmulas e matrizes publicadas pelos
//               fabricantes);
//   Básico    — temperatura, matiz (tint), exposição, contraste com pivô,
//               realces, sombras, brancos, pretos, brilho, saturação, vibração;
//   Rodas     — lift, gamma, gain e offset (cor + luz);
//   Curvas    — RGB (mestra + canais) e de matiz (matiz × matiz, matiz × sat,
//               matiz × luz, luz × sat; a diagonal é o neutro);
//   HSL       — 8 faixas de cor, cada uma com matiz, saturação e luz;
//   Secundária— qualificador por matiz/saturação/luz com correção própria;
//   Criativo  — tonalização dividida, desbotado, nitidez, vinheta, grão;
//   LUT       — um .cube criativo com intensidade;
//   Mistura   — com o original (a "intensidade" do filtro).
//  Os PRESETS são os looks dos Filtros do modo de edição.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

struct GradeUniforms {
    EffectUniforms base{};
    Vec4 g[20]{};
};
static_assert(sizeof(GradeUniforms) == 112 + 20 * 16, "layout std140 da correção de cor");
static_assert(sizeof(GradeUniforms) <= binding::kMaxUniformBytes);

class ColorGrade final : public Effect {
public:
    enum : u32 {
        kLog = 0,
        kTemperature, kTint, kExposure, kContrast, kPivot, kHighlights, kShadows, kWhites, kBlacks, kBrightness,
        kSaturation, kVibrance,
        kLift, kLiftLuma, kGamma, kGammaLuma, kGain, kGainLuma, kOffset, kOffsetLuma,
        kCurves, kHueCurves,
        kHslFirst,                       // 8 faixas × (matiz, saturação, luz)
        kQualifier = kHslFirst + 24, kQHue, kQHueWidth, kQSatLow, kQSatHigh, kQLumaLow, kQLumaHigh, kQSoft,
        kQInvert, kQShow, kQHueShift, kQSaturation, kQExposure,
        kFade, kShadowTint, kHighlightTint, kTintBalance, kSharpen, kVignette, kVignetteMid, kVignetteFeather,
        kGrain, kGrainSize,
        kLut, kLutIntensity, kLutSize, kLutDims, kLutMin, kLutMax,
        kMix,
        // Escondidos: o papel da instância (0 livre, 1 Filtro, 2 Ajustar — os
        // painéis do modo de edição acham "a sua") e o look aplicado (−1 nenhum).
        kRole, kLook,
        kCount,
    };
    static_assert(kLut == 70 && kRole == 77 && kLook == 78, "índices usados pelo motor e pelas interfaces");

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kColorGrade, "Correção de cor", "Cor", EffectClass::Neighborhood};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kProfiles[] = {"Nenhum (Rec.709)", "S-Log3 (Sony)", "LogC3 (ARRI)", "V-Log (Panasonic)",
                                                "Apple Log", "Cineon"};
        constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
        p.add_enum("log_profile", "Perfil da câmera", kProfiles, 6, 0);
        p.add_float("temperature", "Temperatura", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("tint", "Matiz (tint)", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("exposure", "Exposição", 0.0f, -5.0f, 5.0f, kParamAnimatable, "EV");
        p.add_float("contrast", "Contraste", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("pivot", "Pivô do contraste", 46.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("highlights", "Realces", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("shadows", "Sombras", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("whites", "Brancos", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("blacks", "Pretos", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("brightness", "Brilho", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("saturation", "Saturação", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.add_float("vibrance", "Vibração", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        // Rodas: o ponto no disco é a cor (ângulo = matiz, distância = força).
        p.add_point2("lift", "Lift (sombras)", Vec2{0, 0}, -1.0f, 1.0f, kParamAnimatable);
        p.add_float("lift_luma", "Luz do lift", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_point2("gamma", "Gamma (meios-tons)", Vec2{0, 0}, -1.0f, 1.0f, kParamAnimatable);
        p.add_float("gamma_luma", "Luz do gamma", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_point2("gain", "Gain (realces)", Vec2{0, 0}, -1.0f, 1.0f, kParamAnimatable);
        p.add_float("gain_luma", "Luz do gain", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_point2("offset", "Offset (tudo)", Vec2{0, 0}, -1.0f, 1.0f, kParamAnimatable);
        p.add_float("offset_luma", "Luz do offset", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_curve("curves", "Curvas RGB");
        p.add_curve("hue_curves", "Curvas de matiz");
        static const char* const kBands[] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};
        static const char* const kBandNames[] = {"Vermelho", "Laranja", "Amarelo", "Verde", "Ciano", "Azul", "Roxo", "Magenta"};
        static char ids[8][3][24];
        static char labels[8][3][40];
        for (u32 b = 0; b < 8; ++b) {
            static const char* const kWhat[] = {"hue", "sat", "lum"};
            static const char* const kWhatName[] = {"matiz", "saturação", "luz"};
            for (u32 k = 0; k < 3; ++k) {
                std::snprintf(ids[b][k], sizeof(ids[b][k]), "%s_%s", kBands[b], kWhat[k]);
                std::snprintf(labels[b][k], sizeof(labels[b][k]), "%s: %s", kBandNames[b], kWhatName[k]);
                p.add_float(ids[b][k], labels[b][k], 0.0f, -100.0f, 100.0f, kParamAnimatable);
            }
        }
        p.add_bool("qualifier", "Correção secundária", false);
        p.add_angle("q_hue", "Matiz escolhida", 30.0f, 0.0f, 360.0f);
        p.add_float("q_hue_width", "Largura da matiz", 30.0f, 1.0f, 180.0f, kParamAnimatable, "°");
        p.add_float("q_sat_low", "Saturação mínima", 10.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("q_sat_high", "Saturação máxima", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("q_luma_low", "Luz mínima", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("q_luma_high", "Luz máxima", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("q_softness", "Suavidade da seleção", 10.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("q_invert", "Inverter seleção", false);
        p.add_bool("q_show_mask", "Mostrar seleção", false);
        p.add_float("q_hue_shift", "Girar matiz", 0.0f, -180.0f, 180.0f, kParamAnimatable, "°");
        p.add_float("q_saturation", "Saturação da seleção", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("q_exposure", "Exposição da seleção", 0.0f, -3.0f, 3.0f, kParamAnimatable, "EV");
        p.add_float("fade", "Desbotado", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_point2("shadow_tint", "Tom das sombras", Vec2{0, 0}, -1.0f, 1.0f, kParamAnimatable);
        p.add_point2("highlight_tint", "Tom dos realces", Vec2{0, 0}, -1.0f, 1.0f, kParamAnimatable);
        p.add_float("tint_balance", "Equilíbrio dos tons", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("sharpen", "Nitidez", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("vignette", "Vinheta", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("vignette_mid", "Início da vinheta", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("vignette_feather", "Suavidade da vinheta", 50.0f, 1.0f, 100.0f, kAnimPct, "%");
        p.add_float("grain", "Grão", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("grain_size", "Tamanho do grão", 1.5f, 0.5f, 8.0f, kParamAnimatable | kParamPixels, "px");
        p.add_texture_ref("lut", "LUT criativa (.cube)");
        p.add_float("lut_intensity", "Intensidade da LUT", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("lut_size", "LUT size", 2, 2, 65536, kParamHidden);
        p.add_float("lut_dims", "LUT dimensions", 3, 1, 3, kParamHidden);
        ParamSpec domain;
        domain.type = ParamType::Point3D; domain.flags = kParamHidden;
        domain.minValue = -65504; domain.maxValue = 65504;
        domain.id = "lut_min"; domain.label = "LUT min"; domain.defaultValue = ParamValue::vec3(0, 0, 0); p.add(domain);
        domain.id = "lut_max"; domain.label = "LUT max"; domain.defaultValue = ParamValue::vec3(1, 1, 1); p.add(domain);
        p.add_float("mix", "Intensidade", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("role", "Papel", 0.0f, 0.0f, 2.0f, kParamHidden);
        p.add_float("look", "Look", -1.0f, -1.0f, 64.0f, kParamHidden);

        p.add_section_range("basic", "Básico", kTemperature, kVibrance);
        p.add_section_range("wheels", "Rodas de cor", kLift, kOffsetLuma);
        p.add_section_range("curves", "Curvas", kCurves, kHueCurves);
        p.add_section_range("hsl", "HSL", kHslFirst, kHslFirst + 23, kInvalidIndex, false);
        p.add_section_range("secondary", "Correção secundária", kQHue, kQExposure, kQualifier, false);
        p.add_section_range("creative", "Criativo", kFade, kGrainSize);
        p.add_section_range("lut", "LUT", kLut, kLutIntensity);
    }

    /// Os looks dos Filtros (modo de edição): só valores parados, um passo de
    /// desfazer. A intensidade do filtro é a Mistura.
    std::span<const EffectPreset> presets() const noexcept override {
        // Rodas: (cos, sen) da matiz × força. Matiz 0 = vermelho, 0,5 = ciano.
        static constexpr EffectPresetValue kCinema[] = {
            {kContrast, 15}, {kSaturation, 105},
            {kShadowTint, -0.45f, 0}, {kShadowTint, -0.08f, 1},          // ciano-azulado
            {kHighlightTint, 0.40f, 0}, {kHighlightTint, 0.23f, 1}};     // laranja
        static constexpr EffectPresetValue kWarm[] = {{kTemperature, 35}, {kTint, 5}, {kVibrance, 10}};
        static constexpr EffectPresetValue kCool[] = {{kTemperature, -35}, {kTint, -4}, {kVibrance, 5}};
        static constexpr EffectPresetValue kVintage[] = {{kFade, 35}, {kSaturation, 80}, {kTemperature, 15}, {kContrast, -10},
                                                         {kShadowTint, 0.25f, 0}, {kShadowTint, 0.10f, 1}, {kGrain, 20}};
        static constexpr EffectPresetValue kMono[] = {{kSaturation, 0}, {kContrast, 20}};
        static constexpr EffectPresetValue kNoir[] = {{kSaturation, 0}, {kContrast, 45}, {kBlacks, -30}, {kVignette, -35}};
        static constexpr EffectPresetValue kVivid[] = {{kVibrance, 40}, {kSaturation, 115}, {kContrast, 10}};
        static constexpr EffectPresetValue kFaded[] = {{kFade, 50}, {kContrast, -15}, {kSaturation, 85}};
        static constexpr EffectPresetValue kSunset[] = {{kTemperature, 50}, {kTint, 15}, {kSaturation, 110},
                                                        {kHighlightTint, 0.45f, 0}, {kHighlightTint, 0.20f, 1}};
        static constexpr EffectPresetValue kTeal[] = {{kHslFirst + 3 * 3 + 0, 40}, {kHslFirst + 4 * 3 + 0, 20},
                                                      {kHslFirst + 1 * 3 + 1, 15}, {kSaturation, 110}, {kContrast, 8}};
        static constexpr EffectPresetValue kFilm[] = {{kFade, 15}, {kContrast, 10}, {kGrain, 25},
                                                      {kShadowTint, -0.15f, 0}, {kShadowTint, 0.20f, 1},
                                                      {kHighlightTint, 0.20f, 0}, {kHighlightTint, 0.12f, 1}};
        static constexpr EffectPresetValue kAnime[] = {{kVibrance, 50}, {kContrast, 15}, {kSharpen, 30}, {kSaturation, 110}};
        static constexpr EffectPresetValue kMatte[] = {{kBlacks, 25}, {kFade, 20}, {kContrast, -5}};
        static const EffectPreset kPresets[] = {
            {"cinema", "Cinema", kCinema}, {"warm", "Quente", kWarm}, {"cool", "Frio", kCool},
            {"vintage", "Vintage", kVintage}, {"mono", "P&B", kMono}, {"noir", "Noir", kNoir},
            {"vivid", "Vívido", kVivid}, {"faded", "Desbotado", kFaded}, {"sunset", "Pôr do sol", kSunset},
            {"teal", "Verde-azulado", kTeal}, {"film", "Filme", kFilm}, {"anime", "Anime", kAnime},
            {"matte", "Fosco", kMatte},
        };
        return kPresets;
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kContrast] = ParamValue::scalar(15.0f);
        v[kSaturation] = ParamValue::scalar(115.0f);
        v[kShadowTint] = ParamValue::vec2(-0.45f, -0.08f);
        v[kHighlightTint] = ParamValue::vec2(0.40f, 0.23f);
        return true;
    }

    static bool zero(const EffectEval& e, u32 i) noexcept { return std::fabs(finite_or(e.f(i), 0.0f)) < 1e-4f; }
    static bool still(const EffectEval& e, u32 i) noexcept {
        const Vec2 v = e.p2(i);
        return !(std::fabs(finite_or(v.x, 0.0f)) > 1e-4f || std::fabs(finite_or(v.y, 0.0f)) > 1e-4f);
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < kCount || finite_or(e.f(kMix), 100.0f) <= 0.01f) return true;
        if (e.e(kLog) != 0) return false;
        for (u32 i : {kTemperature, kTint, kExposure, kContrast, kHighlights, kShadows, kWhites, kBlacks, kBrightness, kVibrance,
                      kLiftLuma, kGammaLuma, kGainLuma, kOffsetLuma, kFade, kTintBalance, kSharpen, kVignette, kGrain})
            if (!zero(e, i)) return false;
        if (std::fabs(finite_or(e.f(kSaturation), 100.0f) - 100.0f) > 1e-3f) return false;
        for (u32 i : {kLift, kGamma, kGain, kOffset, kShadowTint, kHighlightTint}) if (!still(e, i)) return false;
        for (u32 i = 0; i < 24; ++i) if (!zero(e, kHslFirst + i)) return false;
        if (e.b(kQualifier)) return false;
        if (e.value(kLut).ref) return false;
        if (e.instance) {
            if (const CurveData* c = e.curve(kCurves); c && !c->is_identity()) return false;
            if (const CurveData* c = e.curve(kHueCurves); c && !c->is_identity()) return false;
        } else {
            // Na montagem (sem a instância) vale o que o planejamento resolveu.
            if (e.aux.valid() || e.auxExtra[0].valid()) return false;
        }
        return true;
    }

    f32 input_margin(const EffectEval& e) const noexcept override { return zero(e, kSharpen) ? 0.0f : 1.0f; }

    void resolve_resources(EffectEval& e) const noexcept override {
        if (!e.resources || e.count < kCount) return;
        if (const CurveData* c = e.curve(kCurves); c && !c->is_identity()) e.aux = e.resources->curve_lut(*c);
        if (const CurveData* h = e.curve(kHueCurves); h && !h->is_identity()) {
            Vec4 texels[256];
            for (u32 i = 0; i < 256; ++i) {
                const f32 x = static_cast<f32>(i) / 255.0f;
                texels[i] = Vec4{h->evaluate(0, x), h->evaluate(1, x), h->evaluate(2, x), h->evaluate(3, x)};
            }
            e.auxExtra[0] = e.resources->data_texture(h->hash() ^ 0xC010'6EADE0ull, texels, 256, 1);
        }
        if (e.value(kLut).ref) e.auxExtra[1] = e.resources->cube_lut(AssetId::unpack(e.value(kLut).ref));
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_color_grade_frag, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        GradeUniforms u;
        u.base = base_uniforms(input);
        const f32 fps = e.framesPerSecond > 0.0 ? static_cast<f32>(e.framesPerSecond) : 30.0f;
        u.base.p0 = Vec4{static_cast<f32>(std::min<u32>(e.e(kLog), 5u)), finite_or(e.f(kTemperature), 0) * 0.01f,
                         finite_or(e.f(kTint), 0) * 0.01f, std::clamp(finite_or(e.f(kExposure), 0), -5.0f, 5.0f)};
        u.base.p1 = Vec4{std::clamp(finite_or(e.f(kContrast), 0) * 0.01f, -1.0f, 1.0f), std::clamp(finite_or(e.f(kPivot), 46) * 0.01f, 0.0f, 1.0f),
                         finite_or(e.f(kHighlights), 0) * 0.01f, finite_or(e.f(kShadows), 0) * 0.01f};
        u.base.p2 = Vec4{finite_or(e.f(kWhites), 0) * 0.01f, finite_or(e.f(kBlacks), 0) * 0.01f,
                         finite_or(e.f(kBrightness), 0) * 0.01f, std::max(0.0f, finite_or(e.f(kSaturation), 100) * 0.01f)};
        const bool curves = e.aux.valid(), hue = e.auxExtra[0].valid(), lut = e.auxExtra[1].valid();
        bool hsl = false;
        for (u32 i = 0; i < 24; ++i) hsl |= !zero(e, kHslFirst + i);
        const bool qualifier = e.b(kQualifier);
        const u32 flags = (curves ? 1u : 0u) | (hue ? 2u : 0u) | (lut ? 4u : 0u) | (qualifier ? 8u : 0u)
                        | (qualifier && e.b(kQShow) ? 16u : 0u) | (qualifier && e.b(kQInvert) ? 32u : 0u) | (hsl ? 64u : 0u);
        u.base.p3 = Vec4{finite_or(e.f(kVibrance), 0) * 0.01f, std::clamp(finite_or(e.f(kMix), 100) * 0.01f, 0.0f, 1.0f),
                         static_cast<f32>(e.time_frames() / fps), static_cast<f32>(flags)};
        const Vec2 lift = e.p2(kLift), gamma = e.p2(kGamma), gain = e.p2(kGain), offset = e.p2(kOffset);
        u.g[0] = Vec4{lift.x, lift.y, finite_or(e.f(kLiftLuma), 0) * 0.01f, finite_or(e.f(kGammaLuma), 0) * 0.01f};
        u.g[1] = Vec4{gamma.x, gamma.y, gain.x, gain.y};
        u.g[2] = Vec4{finite_or(e.f(kGainLuma), 0) * 0.01f, offset.x, offset.y, finite_or(e.f(kOffsetLuma), 0) * 0.01f};
        f32 hslValues[24];
        for (u32 i = 0; i < 24; ++i) hslValues[i] = std::clamp(finite_or(e.f(kHslFirst + i), 0) * 0.01f, -1.0f, 1.0f);
        for (u32 i = 0; i < 6; ++i)
            u.g[3 + i] = Vec4{hslValues[i * 4], hslValues[i * 4 + 1], hslValues[i * 4 + 2], hslValues[i * 4 + 3]};
        u.g[9] = Vec4{std::fmod(finite_or(e.f(kQHue), 30) + 3600.0f, 360.0f), std::clamp(finite_or(e.f(kQHueWidth), 30), 1.0f, 180.0f),
                      finite_or(e.f(kQSatLow), 10) * 0.01f, finite_or(e.f(kQSatHigh), 100) * 0.01f};
        u.g[10] = Vec4{finite_or(e.f(kQLumaLow), 0) * 0.01f, finite_or(e.f(kQLumaHigh), 100) * 0.01f,
                       finite_or(e.f(kQSoft), 10) * 0.01f, finite_or(e.f(kQHueShift), 0)};
        u.g[11] = Vec4{finite_or(e.f(kQSaturation), 0) * 0.01f, finite_or(e.f(kQExposure), 0),
                       std::clamp(finite_or(e.f(kFade), 0) * 0.01f, 0.0f, 1.0f), finite_or(e.f(kTintBalance), 0) * 0.01f};
        const Vec2 st = e.p2(kShadowTint), ht = e.p2(kHighlightTint);
        u.g[12] = Vec4{st.x, st.y, ht.x, ht.y};
        u.g[13] = Vec4{std::clamp(finite_or(e.f(kSharpen), 0) * 0.01f, 0.0f, 1.0f), finite_or(e.f(kVignette), 0) * 0.01f,
                       std::clamp(finite_or(e.f(kVignetteMid), 50) * 0.01f, 0.0f, 1.0f), std::clamp(finite_or(e.f(kVignetteFeather), 50) * 0.01f, 0.01f, 1.0f)};
        u.g[14] = Vec4{std::clamp(finite_or(e.f(kGrain), 0) * 0.01f, 0.0f, 1.0f), std::max(0.5f, finite_or(e.f(kGrainSize), 1.5f)),
                       std::clamp(finite_or(e.f(kLutIntensity), 100) * 0.01f, 0.0f, 1.0f), finite_or(e.f(kLutSize), 2)};
        u.g[15] = Vec4{finite_or(e.f(kLutDims), 3), 0, 0, 0};
        const Vec3 lo = e.value(kLutMin).as_vec3(), hi = e.value(kLutMax).as_vec3();
        u.g[16] = Vec4{lo.x, lo.y, lo.z, 0};
        u.g[17] = Vec4{hi.x, hi.y, hi.z, 0};
        u.g[18] = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        const f32 lw = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 lh = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        u.g[19] = Vec4{lw, lh, input.texel_scale_x(), 0};

        out = input;
        out.texture = ctx.texture("correcao-de-cor", input.width, input.height);
        const PassTexture dummy{input.texture, {}, CommonSampler::LinearClamp};
        if (ctx.fullscreen_pass("correcao-de-cor", PassStage::Effects, out.texture, ShaderId::effects_color_grade_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp},
                                 curves ? PassTexture{{}, e.aux, CommonSampler::LinearClamp} : dummy,
                                 hue ? PassTexture{{}, e.auxExtra[0], CommonSampler::LinearClamp} : dummy,
                                 lut ? PassTexture{{}, e.auxExtra[1], CommonSampler::NearestClamp} : dummy},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_color_grade_effect(EffectRegistry& r) { (void)r.add(std::make_unique<ColorGrade>()); }

} // namespace aurea::builtin
