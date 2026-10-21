// =============================================================================
//  Aurea / effects / builtin / EnhanceEffect.cpp
//
//  APRIMORAR (Nitidez, 0.3.0) — aurea.enhance.video.
//
//  Pedido do dono: "um efeito idêntico ao que o Topaz faz, só que ultra
//  otimizado". Os modelos daquele aplicativo são redes treinadas fechadas;
//  saída idêntica não é possível e nada dele (modelo, código, shader, asset)
//  entra aqui. O que este efeito reproduz é o CONTRATO público: os mesmos
//  controles do modo manual dos modelos de aprimoramento (reverter
//  compressão, recuperar detalhes, nitidez, reduzir ruído, remover halos,
//  antisserrilhado/desborrar, adicionar ruído, recuperar detalhe original),
//  os modelos como pesos prontos com os sliders somando a eles, o
//  "Automático" que estima o quadro, e o uso de quadros vizinhos — tudo com
//  algoritmos clássicos rápidos na GPU (notas em build/reference/topaz/).
//
//  A cadeia de passes (todos de tela cheia; nenhum laço sem teto):
//
//   [Automático] análise ¼ (enhance_analyze) → reduções 4×4 até 1 texel por
//                ladrilho (enhance_reduce)
//   pesos 2×1    (enhance_solve) — modelo + sliders (+ estimativa)
//   [temporal]   por vizinho: optical flow atual → vizinho (o Lucas-Kanade em
//                pirâmide do Flow Retime: video/flow_luma, flow_lk,
//                flow_smooth) e a média compensada (enhance_temporal)
//   [compressão] desbloqueio horizontal e vertical (enhance_deblock)
//   meia resol.  (enhance_down) → [ruído] bilateral (enhance_bilateral)
//   [compressão/ruído] anel + pirâmide de detalhe (enhance_recon)
//   final        (enhance_finish): serrilhado/desborrar, detalhes, nitidez,
//                halos, detalhe original, grão e mistura.
//
//  Índices de parâmetro fixos nos projetos salvos (só se acrescenta no fim).
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

/// Os modelos: pesos de (compressão, detalhes, nitidez, ruído, halos,
/// serrilhado/desborrar) em 0..1. O Automático mede na GPU; o Manual é zero.
constexpr u32 kModelAuto = 0, kModelManual = 5;
constexpr f32 kModelWeights[6][6] = {
    {0, 0, 0, 0, 0, 0},                          // Automático (estimado no quadro)
    {0.60f, 0.25f, 0.15f, 0.20f, 0.10f, 0.10f},  // Vídeo comprimido (celular, redes)
    {0.25f, 0.25f, 0.15f, 0.60f, 0.10f, 0.10f},  // Vídeo antigo / ruidoso
    {0.45f, 0.00f, 0.20f, 0.40f, 0.35f, 0.40f},  // Animação
    {0.20f, 0.45f, 0.40f, 0.15f, 0.15f, 0.50f},  // Baixa resolução / borrado
    {0, 0, 0, 0, 0, 0},                          // Manual
};

/// Qualidade: o que muda entre Rápida, Normal e Alta.
struct QualityPlan {
    f32 flowSide;     ///< lado maior da base do optical flow (px)
    u32 iterations;   ///< iterações do Lucas-Kanade por nível
    bool flowSmooth;  ///< regulariza os níveis de cima
    f32 taps;         ///< amostras do remendo da semelhança temporal
    f32 radius;       ///< raio do bilateral (meia resolução)
    bool dering;      ///< filtro do anel do codec
};
constexpr QualityPlan kQualityPlans[3] = {
    {192.0f, 3u, false, 1.0f, 1.0f, false},
    {256.0f, 5u, true, 5.0f, 2.0f, true},
    {384.0f, 6u, true, 9.0f, 3.0f, true},
};

struct AnalyzeUniforms { Vec4 texel; Vec4 tiles; Vec4 grid; };
struct ReduceUniforms { Vec4 size; Vec4 count; };
struct SolveUniforms { Vec4 baseA; Vec4 baseB; Vec4 offA; Vec4 offB; Vec4 flags; };
struct PairUniforms { Vec4 texel; Vec4 flags; };
struct FinishUniforms { Vec4 texel; Vec4 flags; Vec4 grain; Vec4 region; };

/// Optical flow do quadro atual para um vizinho (só esse sentido), na base
/// reduzida: as MESMAS etapas do Movimento suave / Flow Retime (luminância
/// dos dois, pirâmide, Lucas-Kanade do grosso para o fino, regularização).
/// RG = deslocamento em px da base. Inválida se um passe não montou.
FGTexture motion_flow(EffectBuildContext& ctx, FGTexture cur, FGTexture nbr, u32 w, u32 h, const QualityPlan& q,
                      u32& baseW, u32& baseH) noexcept {
    const f32 s = std::min(1.0f, q.flowSide / static_cast<f32>(std::max(1u, std::max(w, h))));
    u32 lw = std::max(8u, static_cast<u32>(std::lround(static_cast<f32>(w) * s)));
    u32 lh = std::max(8u, static_cast<u32>(std::lround(static_cast<f32>(h) * s)));
    baseW = lw;
    baseH = lh;
    FGTexture levels[6];
    u32 sizes[6][2];
    levels[0] = ctx.texture("aprimorar-fluxo-nivel", lw, lh);
    sizes[0][0] = lw;
    sizes[0][1] = lh;
    const Vec4 lumaParams{1.0f / static_cast<f32>(lw), 1.0f / static_cast<f32>(lh), 0, 0};
    if (ctx.fullscreen_pass("aprimorar-fluxo-luma", PassStage::Effects, levels[0], ShaderId::video_flow_luma_frag,
                            {PassTexture{cur}, PassTexture{nbr}}, &lumaParams, sizeof(lumaParams)) == kInvalidIndex)
        return FGTexture{};
    u32 n = 1;
    while (n < 6 && std::min(lw, lh) >= 12) {
        const u32 nw = (lw + 1) / 2, nh = (lh + 1) / 2;
        struct { Vec4 uvMap; Vec4 texel; } dp{Vec4{1, 1, 0, 0}, Vec4{1.0f / static_cast<f32>(lw), 1.0f / static_cast<f32>(lh), 0, 0}};
        levels[n] = ctx.texture("aprimorar-fluxo-nivel", nw, nh);
        if (ctx.fullscreen_pass("aprimorar-fluxo-reduz", PassStage::Effects, levels[n], ShaderId::effects_downsample_frag,
                                {PassTexture{levels[n - 1]}}, &dp, sizeof(dp)) == kInvalidIndex)
            return FGTexture{};
        sizes[n][0] = lw = nw;
        sizes[n][1] = lh = nh;
        ++n;
    }
    FGTexture flow{};
    bool have = false;
    for (u32 i = n; i-- > 0;) {
        const f32 fw = static_cast<f32>(sizes[i][0]), fh = static_cast<f32>(sizes[i][1]);
        const PairUniforms lp{Vec4{1.0f / fw, 1.0f / fh, fw, fh}, Vec4{have ? 1.0f : 0.0f, static_cast<f32>(q.iterations), 0, 0}};
        const FGTexture raw = ctx.texture("aprimorar-fluxo-bruto", sizes[i][0], sizes[i][1]);
        if (ctx.fullscreen_pass("aprimorar-fluxo-lk", PassStage::Effects, raw, ShaderId::video_flow_lk_frag,
                                {PassTexture{levels[i]}, PassTexture{have ? flow : levels[i]}}, &lp, sizeof(lp)) == kInvalidIndex)
            return FGTexture{};
        if (i == 0 || !q.flowSmooth) {
            flow = raw;
            have = true;
            continue;
        }
        const PairUniforms sp{Vec4{1.0f / fw, 1.0f / fh, fw, fh}, Vec4{0.0f, 0.06f, 0, 0}};
        const FGTexture smooth = ctx.texture("aprimorar-fluxo", sizes[i][0], sizes[i][1]);
        if (ctx.fullscreen_pass("aprimorar-fluxo-suave", PassStage::Effects, smooth, ShaderId::video_flow_smooth_frag,
                                {PassTexture{raw, {}, CommonSampler::NearestClamp}, PassTexture{levels[i]}}, &sp, sizeof(sp)) == kInvalidIndex)
            return FGTexture{};
        flow = smooth;
        have = true;
    }
    return flow;
}

class Enhance final : public Effect {
public:
    enum : u32 {
        kModel = 0,
        kRevertCompression, kRecoverDetails, kSharpen, kReduceNoise, kDehalo, kAntialiasDeblur,
        kAddNoise, kGrainSize, kRecoverOriginal,
        kTemporalFrames, kQuality, kMix,
        kCount,
    };
    static_assert(kCount == 13, "índices gravados nos projetos: só se acrescenta no fim");

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kEnhance, "Aprimorar", "Nitidez", EffectClass::Neighborhood};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kModels[] = {"Automático", "Vídeo comprimido", "Vídeo antigo / ruidoso", "Animação",
                                              "Baixa resolução / borrado", "Manual"};
        static const char* const kQualities[] = {"Rápida", "Normal", "Alta"};
        p.add_enum("model", "Modelo", kModels, 6, kModelAuto);
        // 0..100 no slider (o modo manual); digitado vai a −100..100: nos
        // modelos e no Automático o valor SOMA ao peso do modelo (negativo tira).
        auto addStage = [&](const char* id, const char* label) {
            p.add_float(id, label, 0.0f, 0.0f, 100.0f, kAnimPct, "%");
            p.typed_range(-100.0f, 100.0f);
        };
        addStage("revert_compression", "Reverter compressão");
        addStage("recover_details", "Recuperar detalhes");
        addStage("sharpen", "Nitidez");
        addStage("reduce_noise", "Reduzir ruído");
        addStage("dehalo", "Remover halos");
        addStage("antialias_deblur", "Antisserrilhado / desborrar");
        p.add_float("add_noise", "Adicionar ruído", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("grain_size", "Tamanho do grão", 1.0f, 0.5f, 4.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("recover_original_detail", "Recuperar detalhe original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_int("temporal_frames", "Quadros vizinhos", 1, 0, 3, kParamNone);
        p.add_enum("quality", "Qualidade", kQualities, 3, 1);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kAnimPct, "%");

        p.add_section_range("adjust", "Ajustes", kRevertCompression, kAntialiasDeblur);
        p.add_section_range("grain", "Grão e detalhe original", kAddNoise, kRecoverOriginal);
        p.add_section_range("time", "Tempo e qualidade", kTemporalFrames, kQuality, kInvalidIndex, false);
        p.set_tip(kModel, "Automático estima compressão, ruído e borrado a cada quadro. Nos modelos e no "
                          "Automático os ajustes somam ao modelo; no Manual eles são os valores.");
        p.set_tip(kTemporalFrames, "Quadros de cada lado usados pela redução de ruído, alinhados pelo movimento. "
                                   "0 = só o quadro atual.");
    }

    std::span<const EffectPreset> presets() const noexcept override {
#define AUREA_ENHANCE_ZERO {kRevertCompression, 0}, {kRecoverDetails, 0}, {kSharpen, 0}, {kReduceNoise, 0}, \
                           {kDehalo, 0}, {kAntialiasDeblur, 0}
        static constexpr EffectPresetValue kAuto[] = {{kModel, 0}, AUREA_ENHANCE_ZERO, {kAddNoise, 0}, {kRecoverOriginal, 0},
                                                      {kTemporalFrames, 1}, {kQuality, 1}, {kMix, 100}};
        static constexpr EffectPresetValue kSocial[] = {{kModel, 1}, AUREA_ENHANCE_ZERO, {kAddNoise, 0}, {kRecoverOriginal, 0},
                                                        {kTemporalFrames, 1}, {kQuality, 1}, {kMix, 100}};
        static constexpr EffectPresetValue kRestore[] = {{kModel, 2}, AUREA_ENHANCE_ZERO, {kAddNoise, 8}, {kGrainSize, 1.2f},
                                                         {kRecoverOriginal, 0}, {kTemporalFrames, 2}, {kQuality, 2}, {kMix, 100}};
        static constexpr EffectPresetValue kAnime[] = {{kModel, 3}, AUREA_ENHANCE_ZERO, {kAddNoise, 0}, {kRecoverOriginal, 0},
                                                       {kTemporalFrames, 1}, {kQuality, 1}, {kMix, 100}};
        static constexpr EffectPresetValue kUpscaled[] = {{kModel, 4}, {kRevertCompression, 0}, {kRecoverDetails, 0},
                                                          {kSharpen, 10}, {kReduceNoise, 0}, {kDehalo, 0}, {kAntialiasDeblur, 0},
                                                          {kAddNoise, 0}, {kRecoverOriginal, 0}, {kTemporalFrames, 1},
                                                          {kQuality, 1}, {kMix, 100}};
        static constexpr EffectPresetValue kFilm[] = {{kModel, 2}, AUREA_ENHANCE_ZERO, {kAddNoise, 30}, {kGrainSize, 1.6f},
                                                      {kRecoverOriginal, 15}, {kTemporalFrames, 2}, {kQuality, 1}, {kMix, 100}};
#undef AUREA_ENHANCE_ZERO
        static const EffectPreset kPresets[] = {
            {"auto", "Automático", kAuto},
            {"social", "Celular e redes sociais", kSocial},
            {"restore", "Restaurar vídeo antigo", kRestore},
            {"anime", "Animação limpa", kAnime},
            {"upscaled", "Vídeo ampliado", kUpscaled},
            {"film", "Filme com grão", kFilm},
        };
        return kPresets;
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        if (v.size() < kCount) return false;
        v[kModel] = ParamValue::scalar(static_cast<f32>(kModelManual));
        v[kRevertCompression] = ParamValue::scalar(40.0f);
        v[kRecoverDetails] = ParamValue::scalar(50.0f);
        v[kSharpen] = ParamValue::scalar(60.0f);
        v[kReduceNoise] = ParamValue::scalar(35.0f);
        v[kDehalo] = ParamValue::scalar(20.0f);
        v[kAntialiasDeblur] = ParamValue::scalar(25.0f);
        return true;
    }

    static u32 model_of(const EffectEval& e) noexcept { return std::min<u32>(e.e(kModel), kModelManual); }
    static f32 slider(const EffectEval& e, u32 i) noexcept {
        return std::clamp(finite_or(e.f(i), 0.0f) * 0.01f, -1.0f, 1.0f);
    }
    /// Peso final de uma etapa (0..1) fora do Automático.
    static f32 stage(const EffectEval& e, u32 model, u32 s) noexcept {
        return std::clamp(kModelWeights[model][s] + slider(e, kRevertCompression + s), 0.0f, 1.0f);
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < kCount) return true;
        if (finite_or(e.f(kMix), 100.0f) <= 0.01f) return true;
        if (model_of(e) != kModelManual) return false;
        for (u32 i : {kRevertCompression, kRecoverDetails, kSharpen, kReduceNoise, kDehalo, kAntialiasDeblur, kAddNoise,
                      kRecoverOriginal})
            if (finite_or(e.f(i), 0.0f) > 1e-4f) return false;
        return true;
    }

    f32 input_margin(const EffectEval&) const noexcept override { return 12.0f; }

    bool wants_neighbors() const noexcept override { return true; }

    u32 temporal_neighbors(const EffectEval& e, i32* offsets) const noexcept override {
        if (!offsets || is_identity(e)) return 0;
        const i32 n = std::clamp(e.value(kTemporalFrames).as_int(), 0, 3);
        if (n == 0) return 0;
        const u32 model = model_of(e);
        // O tempo é da redução de ruído: sem ela (fora do Automático), só espacial.
        if (model != kModelAuto && stage(e, model, 3) <= 0.0f) return 0;
        u32 count = 0;
        for (i32 k = 1; k <= n; ++k) {
            offsets[count++] = -k;
            offsets[count++] = k;
        }
        return count;
    }

    void resolve_resources(EffectEval& e) const noexcept override {
        // A fração das amostras que o preview aguenta agora (calor, AUTO):
        // abaixo da metade, o temporal cai para o plano da qualidade Rápida.
        e.auxInfo = Vec4{e.resources ? e.resources->effect_quality() : 1.0f, 0, 0, 1};
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        for (ShaderId id : {ShaderId::effects_enhance_analyze_frag, ShaderId::effects_enhance_reduce_frag,
                            ShaderId::effects_enhance_solve_frag, ShaderId::effects_enhance_temporal_frag,
                            ShaderId::effects_enhance_deblock_frag, ShaderId::effects_enhance_down_frag,
                            ShaderId::effects_enhance_bilateral_frag, ShaderId::effects_enhance_recon_frag,
                            ShaderId::effects_enhance_finish_frag, ShaderId::video_flow_luma_frag,
                            ShaderId::effects_downsample_frag, ShaderId::video_flow_lk_frag, ShaderId::video_flow_smooth_frag})
            out.push_back(PipelineKey::fullscreen(id, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        if (!input.valid() || e.count < kCount) return Errc::InvalidArgument;
        const u32 W = input.width, H = input.height;
        const f32 fw = static_cast<f32>(W), fh = static_cast<f32>(H);
        const u32 model = model_of(e);
        const bool automatic = model == kModelAuto;
        const f32 adaptive = e.auxInfo.w > 0.5f ? e.auxInfo.x : 1.0f;
        const u32 qualityIndex = adaptive < 0.5f ? 0u : std::min<u32>(e.e(kQuality), 2u);
        const QualityPlan& q = kQualityPlans[qualityIndex];

        // Etapas que podem agir: no Automático todas (quem decide é a GPU).
        bool live[6];
        for (u32 s = 0; s < 6; ++s) live[s] = automatic || stage(e, model, s) > 1e-4f;
        const bool liveCompression = live[0], liveNoise = live[3];

        // --- Pesos (2×1) ----------------------------------------------------------
        FGTexture stats{};
        if (automatic) {
            stats = analyze(ctx, input);
            if (!stats.valid()) return Errc::PipelineCompileFailed;
        }
        SolveUniforms su{};
        if (!automatic) {
            su.baseA = Vec4{kModelWeights[model][0], kModelWeights[model][1], kModelWeights[model][2], kModelWeights[model][3]};
            su.baseB = Vec4{kModelWeights[model][4], kModelWeights[model][5], 0, 0};
        }
        su.offA = Vec4{slider(e, kRevertCompression), slider(e, kRecoverDetails), slider(e, kSharpen), slider(e, kReduceNoise)};
        su.offB = Vec4{slider(e, kDehalo), slider(e, kAntialiasDeblur), 0, 0};
        su.flags = Vec4{automatic ? 1.0f : 0.0f, 0, 0, 0};
        const FGTexture weights = ctx.texture("aprimorar-pesos", 2, 1);
        if (ctx.fullscreen_pass("aprimorar-pesos", PassStage::Effects, weights, ShaderId::effects_enhance_solve_frag,
                                {PassTexture{stats.valid() ? stats : input.texture, {}, CommonSampler::NearestClamp}},
                                &su, sizeof(su)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        const PassTexture w{weights, {}, CommonSampler::NearestClamp};

        FGTexture cur = input.texture;
        bool curLinear = true;

        // --- Temporal: um vizinho por passe, do mais perto para o mais longe ------
        if (liveNoise && !ctx.neighbors().empty()) {
            EffectBuildContext::NeighborImage order[kMaxTemporalNeighbors];
            u32 count = 0;
            for (const EffectBuildContext::NeighborImage& nb : ctx.neighbors()) {
                if (count >= kMaxTemporalNeighbors || !nb.image.valid() || nb.offset == 0) continue;
                // O vizinho tem de cobrir a MESMA área na MESMA densidade.
                if (nb.image.width != W || nb.image.height != H
                    || std::fabs(nb.image.region.x - input.region.x) > 0.5f || std::fabs(nb.image.region.y - input.region.y) > 0.5f
                    || std::fabs(nb.image.region.w - input.region.w) > 0.5f || std::fabs(nb.image.region.h - input.region.h) > 0.5f)
                    continue;
                order[count++] = nb;
            }
            std::stable_sort(order, order + count, [](const auto& a, const auto& b) {
                return std::abs(a.offset) < std::abs(b.offset);
            });
            for (u32 k = 0; k < count; ++k) {
                u32 bw = 0, bh = 0;
                const FGTexture flow = motion_flow(ctx, input.texture, order[k].image.texture, W, H, q, bw, bh);
                if (!flow.valid()) continue;
                const f32 distance = 1.0f / (1.0f + 0.15f * static_cast<f32>(std::abs(order[k].offset) - 1));
                const PairUniforms tu{Vec4{1.0f / fw, 1.0f / fh, 1.0f / static_cast<f32>(bw), 1.0f / static_cast<f32>(bh)},
                                      Vec4{curLinear ? 1.0f : 0.0f, q.taps, distance, 0}};
                const FGTexture acc = ctx.texture("aprimorar-tempo", W, H);
                if (ctx.fullscreen_pass("aprimorar-tempo", PassStage::Effects, acc, ShaderId::effects_enhance_temporal_frag,
                                        {PassTexture{cur}, PassTexture{order[k].image.texture}, PassTexture{flow}, w},
                                        &tu, sizeof(tu)) == kInvalidIndex)
                    return Errc::PipelineCompileFailed;
                cur = acc;
                curLinear = false;
            }
        }

        // --- Reverter compressão: os blocos, um eixo por passe ---------------------
        if (liveCompression) {
            for (u32 axis = 0; axis < 2; ++axis) {
                const PairUniforms du{Vec4{1.0f / fw, 1.0f / fh, fw, fh},
                                      Vec4{axis == 0 ? input.texel_scale_x() : input.texel_scale_y(),
                                           axis == 0 ? input.region.x : input.region.y, static_cast<f32>(axis),
                                           curLinear ? 1.0f : 0.0f}};
                const FGTexture next = ctx.texture("aprimorar-blocos", W, H);
                if (ctx.fullscreen_pass("aprimorar-blocos", PassStage::Effects, next, ShaderId::effects_enhance_deblock_frag,
                                        {PassTexture{cur}, w}, &du, sizeof(du)) == kInvalidIndex)
                    return Errc::PipelineCompileFailed;
                cur = next;
                curLinear = false;
            }
        }

        // --- Meia resolução (base do ruído, do detalhe e dos halos) -----------------
        const u32 hw = std::max(1u, (W + 1) / 2), hh = std::max(1u, (H + 1) / 2);
        FGTexture half = ctx.texture("aprimorar-meia", hw, hh);
        {
            const PairUniforms hu{Vec4{1.0f / fw, 1.0f / fh, 0, 0}, Vec4{curLinear ? 1.0f : 0.0f, 0, 0, 0}};
            if (ctx.fullscreen_pass("aprimorar-meia", PassStage::Effects, half, ShaderId::effects_enhance_down_frag,
                                    {PassTexture{cur}}, &hu, sizeof(hu)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
        }
        if (liveNoise) {
            const PairUniforms bu{Vec4{1.0f / static_cast<f32>(hw), 1.0f / static_cast<f32>(hh), 0, 0}, Vec4{q.radius, 0, 0, 0}};
            const FGTexture filtered = ctx.texture("aprimorar-ruido", hw, hh);
            if (ctx.fullscreen_pass("aprimorar-ruido", PassStage::Effects, filtered, ShaderId::effects_enhance_bilateral_frag,
                                    {PassTexture{half}, w}, &bu, sizeof(bu)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            half = filtered;
        }
        const bool dering = liveCompression && q.dering;
        if (liveNoise || dering) {
            const PairUniforms ru{Vec4{1.0f / fw, 1.0f / fh, 0, 0},
                                  Vec4{curLinear ? 1.0f : 0.0f, dering ? 1.0f : 0.0f, liveNoise ? 1.0f : 0.0f, 0}};
            const FGTexture clean = ctx.texture("aprimorar-limpo", W, H);
            if (ctx.fullscreen_pass("aprimorar-limpo", PassStage::Effects, clean, ShaderId::effects_enhance_recon_frag,
                                    {PassTexture{cur}, PassTexture{half}, w}, &ru, sizeof(ru)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            cur = clean;
            curLinear = false;
        }

        // --- Final --------------------------------------------------------------------
        FinishUniforms fu{};
        fu.texel = Vec4{1.0f / fw, 1.0f / fh, 1.0f / static_cast<f32>(hw), 1.0f / static_cast<f32>(hh)};
        fu.flags = Vec4{curLinear ? 1.0f : 0.0f, std::clamp(finite_or(e.f(kMix), 100.0f) * 0.01f, 0.0f, 1.0f),
                        std::clamp(finite_or(e.f(kRecoverOriginal), 0.0f) * 0.01f, 0.0f, 1.0f), 0};
        const f64 frame = std::floor(e.time_frames());
        fu.grain = Vec4{std::clamp(finite_or(e.f(kAddNoise), 0.0f) * 0.01f, 0.0f, 1.0f) * 0.10f,
                        std::clamp(finite_or(e.f(kGrainSize), 1.0f), 0.25f, 16.0f),
                        static_cast<f32>(std::fmod(std::fabs(std::isfinite(frame) ? frame : 0.0), 16777216.0)),
                        input.texel_scale_x()};
        fu.region = Vec4{input.region.x, input.region.y, 0, 0};
        out = input;
        out.history = FGTexture{};
        out.neighbors = TemporalNeighbors{};
        out.texture = ctx.texture("aprimorar", W, H);
        if (ctx.fullscreen_pass("aprimorar", PassStage::Effects, out.texture, ShaderId::effects_enhance_finish_frag,
                                {PassTexture{cur}, PassTexture{half}, PassTexture{input.texture}, w},
                                &fu, sizeof(fu)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }

private:
    /// A estimativa do Automático: análise em ¼ (dois ladrilhos) e as reduções
    /// até um texel por ladrilho (2×1).
    static FGTexture analyze(EffectBuildContext& ctx, const LayerImage& input) noexcept {
        const u32 W = input.width, H = input.height;
        u32 tw = std::max(1u, (W + 3) / 4), th = std::max(1u, (H + 3) / 4);
        const f32 s = input.texel_scale_x();
        AnalyzeUniforms au{};
        au.texel = Vec4{1.0f / static_cast<f32>(W), 1.0f / static_cast<f32>(H), static_cast<f32>(W), static_cast<f32>(H)};
        au.tiles = Vec4{static_cast<f32>(tw), static_cast<f32>(th), 0, 0};
        au.grid = Vec4{s, input.region.x, input.region.y, 8.0f * s >= 3.0f ? 1.0f : 0.0f};
        FGTexture cur = ctx.texture("aprimorar-analise", tw * 2, th);
        if (ctx.fullscreen_pass("aprimorar-analise", PassStage::Effects, cur, ShaderId::effects_enhance_analyze_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}}, &au, sizeof(au)) == kInvalidIndex)
            return FGTexture{};
        // Teto: 1080p desce em 5 reduções; 8K em 7. Nunca mais de 12.
        for (u32 guard = 0; (tw > 1 || th > 1) && guard < 12; ++guard) {
            const u32 ow = std::max(1u, (tw + 3) / 4), oh = std::max(1u, (th + 3) / 4);
            const ReduceUniforms ru{Vec4{static_cast<f32>(tw), static_cast<f32>(th), static_cast<f32>(ow), static_cast<f32>(oh)},
                                    Vec4{2.0f, 0, 0, 0}};
            const FGTexture next = ctx.texture("aprimorar-reducao", ow * 2, oh);
            if (ctx.fullscreen_pass("aprimorar-reducao", PassStage::Effects, next, ShaderId::effects_enhance_reduce_frag,
                                    {PassTexture{cur, {}, CommonSampler::NearestClamp}}, &ru, sizeof(ru)) == kInvalidIndex)
                return FGTexture{};
            cur = next;
            tw = ow;
            th = oh;
        }
        return cur;
    }
};

} // namespace

void register_enhance_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Enhance>()); }

} // namespace aurea::builtin
