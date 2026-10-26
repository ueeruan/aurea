// =============================================================================
//  Aurea / effects / builtin / HyperEffects.cpp
//
//  AureaHyper (0.3.2) no editor: os efeitos de camada Hyper Upscale, Hyper
//  Clean e Hyper Motion (chaves em aurea/hyper/Hyper.hpp). Na prévia e no
//  export do editor eles rodam SEMPRE o caminho Turbo (estes passes); o
//  parâmetro "Caminho" (Turbo/Lite/Max) é lido pelo job Hyper da Home e
//  guardado no projeto. Os mesmos passes renderizam o job: a prévia de um
//  quadro e o export Turbo do mesmo quadro são o mesmo cálculo.
//
//  Hyper Upscale — General: FSR 1 (EASU + RCAS, AMD, MIT); Anime: EASU +
//    afinamento de linhas + endurecimento de bordas (ideia do Anime4K 0.9,
//    MIT). A densidade de saída é a que a tela pede (a escala da camada na
//    composição), limitada a 2x/4x da entrada e ao teto de textura.
//  Hyper Clean — desentrelaçar (pente + direção de borda, detecção
//    automática), desbloquear (grade 8x8) e reduzir ruído no tempo (3 a 5
//    quadros alinhados por blocos 16x16, pesos de semelhança) + bilateral.
//  Hyper Motion — DECLARATIVO como o Flow Retime: o renderer interpola na
//    FONTE (Renderer.cpp, frameBlend 3) entre os quadros vizinhos quando a
//    composição pede um instante entre eles (60 fps de um vídeo 30, câmera
//    lenta xN dentro da camada). O som da camada não muda (como no Flow
//    Retime); o job da Home sai mudo na câmera lenta.
//
//  Índices de parâmetro fixos nos projetos salvos (só se acrescenta no fim).
// =============================================================================
#include "BuiltinEffects.hpp"
#include "HyperFx.hpp"

#include "aurea/hyper/Hyper.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::hyper::fx {

void field_size(u32 w, u32 h, u32& fw, u32& fh) noexcept {
    fw = std::max(1u, (w + kBlock - 1) / kBlock);
    fh = std::max(1u, (h + kBlock - 1) / kBlock);
}

FGTexture luma_pair(EffectBuildContext& ctx, FGTexture a, FGTexture b, u32 w, u32 h, bool aPerc, bool bPerc,
                    PassStage stage) noexcept {
    const FGTexture out = ctx.texture("hyper-luma", w, h);
    const Vec4 flags{aPerc ? 1.0f : 0.0f, bPerc ? 1.0f : 0.0f, 0, 0};
    if (ctx.fullscreen_pass("hyper-luma", stage, out, ShaderId::hyper_me_luma_frag,
                            {PassTexture{a, {}, CommonSampler::NearestClamp}, PassTexture{b, {}, CommonSampler::NearestClamp}},
                            &flags, sizeof(flags)) == kInvalidIndex)
        return FGTexture{};
    return out;
}

FGTexture motion_field(EffectBuildContext& ctx, FGTexture luma, u32 w, u32 h, bool backward, PassStage stage) noexcept {
    if (!luma.valid()) return FGTexture{};
    // Pirâmide: 1, 1/2, 1/4, 1/8 (caixa 4x4 de effects/downsample.frag).
    FGTexture lv[4];
    u32 lw[4], lh[4];
    lv[0] = luma;
    lw[0] = w;
    lh[0] = h;
    u32 levels = 1;
    for (u32 k = 1; k < 4; ++k) {
        if (std::min(lw[k - 1], lh[k - 1]) < 16) break;
        lw[k] = (lw[k - 1] + 1) / 2;
        lh[k] = (lh[k - 1] + 1) / 2;
        struct { Vec4 uvMap; Vec4 texel; } dp{Vec4{1, 1, 0, 0},
            Vec4{1.0f / static_cast<f32>(lw[k - 1]), 1.0f / static_cast<f32>(lh[k - 1]), 0, 0}};
        lv[k] = ctx.texture("hyper-piramide", lw[k], lh[k]);
        if (ctx.fullscreen_pass("hyper-piramide", stage, lv[k], ShaderId::effects_downsample_frag,
                                {PassTexture{lv[k - 1]}}, &dp, sizeof(dp)) == kInvalidIndex)
            return FGTexture{};
        ++levels;
    }
    u32 fw = 0, fh = 0;
    field_size(w, h, fw, fh);
    struct MeUniforms { Vec4 level; Vec4 search; Vec4 block; Vec4 sub; };
    FGTexture field{};
    for (u32 k = levels; k-- > 0;) {
        const bool top = k + 1 == levels;
        const f32 s = static_cast<f32>(1u << k);
        MeUniforms u{};
        u.level = Vec4{s, static_cast<f32>(lw[k]), static_cast<f32>(lh[k]), top ? 0.0f : 1.0f};
        // Grosso: só a busca completa (+-6 texels, +-48 px no 1/8), janela 7x7.
        // 1/4: preditores +-2 E a busca completa +-4 em volta do zero (o objeto
        // pequeno e rápido que o 1/8 não viu). 1/2: preditores +-2. Cheio:
        // preditores +-1, janela 17x17 em passo 2 e meio pixel.
        // Janelas do tamanho do BLOCO (~16-20 px cheios; 40 no 1/8): um objeto
        // do tamanho de um bloco manda no custo do próprio bloco.
        f32 predR = 1.0f, half = 2.0f, step = 1.0f, full = 0.0f, sub = 0.0f;
        if (top) { predR = 0.0f; full = levels >= 3 ? 6.0f : 8.0f; }
        else if (k == 2) { full = 4.0f; }
        else if (k == 1) { step = 2.0f; }
        else { step = 4.0f; sub = 0.5f; }
        u.search = Vec4{predR, half, step, backward ? 1.0f : 0.0f};
        u.block = Vec4{static_cast<f32>(kBlock), static_cast<f32>(fw), static_cast<f32>(fh), 0.5f};
        u.sub = Vec4{sub, full, 0, 0};
        const FGTexture next = ctx.texture("hyper-campo", fw, fh);
        if (ctx.fullscreen_pass("hyper-busca", stage, next, ShaderId::hyper_me_search_frag,
                                {PassTexture{lv[k], {}, CommonSampler::LinearClamp},
                                 PassTexture{field.valid() ? field : lv[k], {}, CommonSampler::NearestClamp}},
                                &u, sizeof(u)) == kInvalidIndex)
            return FGTexture{};
        field = next;
        if (top && levels >= 3) {
            // Mediana SÓ no grosso (o ponto de partida dos outros): no fim ela
            // apagaria o objeto de 1-2 blocos (o vizinho parado ganha a mediana);
            // o vetor errado de um bloco solto é recusado pixel a pixel na
            // interpolação (o candidato tem de casar as duas pontas).
            const Vec4 mp{static_cast<f32>(fw), static_cast<f32>(fh), 0, 0};
            const FGTexture med = ctx.texture("hyper-campo", fw, fh);
            if (ctx.fullscreen_pass("hyper-mediana", stage, med, ShaderId::hyper_me_median_frag,
                                    {PassTexture{field, {}, CommonSampler::NearestClamp}}, &mp, sizeof(mp)) == kInvalidIndex)
                return FGTexture{};
            field = med;
        }
    }
    return field;
}

bool bidirectional_field(EffectBuildContext& ctx, FGTexture a, FGTexture b, u32 w, u32 h, FGTexture target,
                         PassStage stage) noexcept {
    const FGTexture luma = luma_pair(ctx, a, b, w, h, false, false, stage);
    const FGTexture fwd = motion_field(ctx, luma, w, h, false, stage);
    const FGTexture bwd = motion_field(ctx, luma, w, h, true, stage);
    if (!fwd.valid() || !bwd.valid()) return false;
    return ctx.fullscreen_pass("hyper-campo-junta", stage, target, ShaderId::hyper_me_pack_frag,
                               {PassTexture{fwd, {}, CommonSampler::NearestClamp}, PassTexture{bwd, {}, CommonSampler::NearestClamp}},
                               nullptr, 0) != kInvalidIndex;
}

FGTexture interpolate(EffectBuildContext& ctx, FGTexture a, FGTexture b, FGTexture field, FGTexture luma, u32 w, u32 h,
                      f32 t, PassStage stage) noexcept {
    if (!field.valid()) return FGTexture{};
    if (!luma.valid()) luma = luma_pair(ctx, a, b, w, h, false, false, stage);
    if (!luma.valid()) return FGTexture{};
    u32 fw = 0, fh = 0;
    field_size(w, h, fw, fh);
    struct { Vec4 size; Vec4 field; Vec4 tune; } u{
        Vec4{static_cast<f32>(w), static_cast<f32>(h), 1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h)},
        Vec4{static_cast<f32>(kBlock), static_cast<f32>(fw), static_cast<f32>(fh), std::clamp(t, 0.0f, 1.0f)},
        Vec4{0.06f, 1.5f, 0, 0}};
    const FGTexture out = ctx.texture("hyper-quadro-meio", w, h);
    if (ctx.fullscreen_pass("hyper-interpola", stage, out, ShaderId::hyper_mi_interp_frag,
                            {PassTexture{a, {}, CommonSampler::LinearClamp}, PassTexture{b, {}, CommonSampler::LinearClamp},
                             PassTexture{field, {}, CommonSampler::LinearClamp}, PassTexture{luma, {}, CommonSampler::LinearClamp}},
                            &u, sizeof(u)) == kInvalidIndex)
        return FGTexture{};
    return out;
}

} // namespace aurea::hyper::fx

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

const char* const kPathLabels[] = {"Turbo (prévia)", "Lite", "Max"};

struct SizeU { Vec4 size; };
struct TwoU { Vec4 a; Vec4 b; };
struct ThreeU { Vec4 a; Vec4 b; Vec4 c; };

Vec4 size_vec(u32 w, u32 h) noexcept {
    return Vec4{static_cast<f32>(w), static_cast<f32>(h), 1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h)};
}

// =============================================================================
// Hyper Upscale
// =============================================================================
class HyperUpscale final : public Effect {
public:
    enum : u32 { kScale = 0, kMode, kSharpness, kPath, kCount };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{hyper::effect_keys::kHyperUpscale, "Hyper Upscale", "Hyper", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kScales[] = {"2×", "4×"};
        static const char* const kModes[] = {"Geral", "Anime"};
        p.add_enum("scale", "Escala", kScales, 2, 0);
        p.add_enum("mode", "Conteúdo", kModes, 2, 0);
        p.add_float("sharpness", "Nitidez", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_enum("path", "Caminho", kPathLabels, 3, 0);
        p.set_tip(kScale, "Amplia a camada quando ela aparece maior que a fonte (a escala da camada na composição). "
                          "Sem ampliação na tela, só a nitidez age.");
        p.set_tip(kPath, "A prévia usa sempre o Turbo. Lite e Max (redes) valem no job Hyper da Home; "
                         "no export do editor o Turbo é usado.");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        if (v.size() < kCount) return false;
        v[kSharpness] = ParamValue::scalar(80.0f);
        return true;
    }
    f32 input_margin(const EffectEval&) const noexcept override { return 3.0f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        for (ShaderId id : {ShaderId::hyper_to_perc_frag, ShaderId::hyper_fsr_easu_frag, ShaderId::hyper_fsr_rcas_frag,
                            ShaderId::hyper_a4k_thin_frag, ShaderId::hyper_a4k_grad_frag, ShaderId::hyper_a4k_refine_frag})
            out.push_back(PipelineKey::fullscreen(id, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        if (!input.valid() || e.count < kCount) return Errc::InvalidArgument;
        const u32 factor = e.e(kScale) >= 1 ? 4u : 2u;
        const bool anime = e.e(kMode) >= 1;
        const f32 sharp = std::clamp(finite_or(e.f(kSharpness), 50.0f) * 0.01f, 0.0f, 1.0f);
        const u32 W = input.width, H = input.height;
        const f32 k = std::max(input.texel_scale_x(), 1e-3f);
        // Densidade que a tela pede (px da composição por px da camada).
        f32 onScreen = static_cast<f32>(factor);
        if (e.placement && !e.placement->inScene3d) {
            const Mat4& m = e.placement->compFromLayer;
            const f32 sx = std::sqrt(m.col[0].x * m.col[0].x + m.col[0].y * m.col[0].y);
            const f32 sy = std::sqrt(m.col[1].x * m.col[1].x + m.col[1].y * m.col[1].y);
            if (std::isfinite(sx) && std::isfinite(sy) && std::max(sx, sy) > 0.0f) onScreen = std::max(sx, sy);
        }
        f32 ratio = std::clamp(onScreen / k, 1.0f, static_cast<f32>(factor));
        // Teto: lado de textura e ~16,7 MP (4096²) por camada.
        const u32 maxTex = std::max(1u, ctx.max_texture_size());
        ratio = std::min(ratio, static_cast<f32>(maxTex) / static_cast<f32>(std::max(W, H)));
        ratio = std::min(ratio, std::sqrt(16777216.0f / static_cast<f32>(static_cast<u64>(W) * H)));
        ratio = std::max(ratio, 1.0f);
        const u32 oW = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(W) * ratio)));
        const u32 oH = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(H) * ratio)));

        FGTexture cur = ctx.texture("hyper-perceptual", W, H);
        if (ctx.fullscreen_pass("hyper-perceptual", PassStage::Effects, cur, ShaderId::hyper_to_perc_frag,
                                {PassTexture{input.texture, {}, CommonSampler::NearestClamp}}, nullptr, 0) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        u32 cw = W, ch = H;
        // EASU: até 2,5x numa passada (480p -> 1080p é uma); 4x = duas.
        while (cw < oW || ch < oH) {
            const bool last = static_cast<f32>(oW) <= 2.5f * static_cast<f32>(cw) && static_cast<f32>(oH) <= 2.5f * static_cast<f32>(ch);
            const u32 nw = last ? oW : std::min(oW, cw * 2), nh = last ? oH : std::min(oH, ch * 2);
            const TwoU u{size_vec(cw, ch), size_vec(nw, nh)};
            const FGTexture next = ctx.texture("hyper-easu", nw, nh);
            if (ctx.fullscreen_pass("hyper-easu", PassStage::Effects, next, ShaderId::hyper_fsr_easu_frag,
                                    {PassTexture{cur, {}, CommonSampler::NearestClamp}}, &u, sizeof(u)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            cur = next;
            cw = nw;
            ch = nh;
        }
        out = input;
        out.history = FGTexture{};
        out.neighbors = TemporalNeighbors{};
        out.width = cw;
        out.height = ch;
        if (anime) {
            const TwoU tu{size_vec(cw, ch), Vec4{0.20f + 0.25f * sharp, 0, 0, 0}};
            const FGTexture thin = ctx.texture("hyper-linhas", cw, ch);
            if (ctx.fullscreen_pass("hyper-linhas", PassStage::Effects, thin, ShaderId::hyper_a4k_thin_frag,
                                    {PassTexture{cur, {}, CommonSampler::NearestClamp}}, &tu, sizeof(tu)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            const SizeU su{size_vec(cw, ch)};
            const FGTexture grad = ctx.texture("hyper-gradiente", cw, ch);
            if (ctx.fullscreen_pass("hyper-gradiente", PassStage::Effects, grad, ShaderId::hyper_a4k_grad_frag,
                                    {PassTexture{thin, {}, CommonSampler::NearestClamp}}, &su, sizeof(su)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            const TwoU ru{size_vec(cw, ch), Vec4{0.45f + 0.55f * sharp, 1.0f, 0, 0}};
            out.texture = ctx.texture("hyper-upscale", cw, ch);
            if (ctx.fullscreen_pass("hyper-bordas", PassStage::Effects, out.texture, ShaderId::hyper_a4k_refine_frag,
                                    {PassTexture{thin, {}, CommonSampler::NearestClamp}, PassTexture{grad, {}, CommonSampler::NearestClamp}},
                                    &ru, sizeof(ru)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            return OkStatus;
        }
        // RCAS: nitidez em paradas (0 = máxima); 50% = 1 parada.
        const f32 stops = 2.0f * (1.0f - sharp);
        const TwoU ru{size_vec(cw, ch), Vec4{std::exp2(-stops), 1.0f, 1.0f, 0}};
        out.texture = ctx.texture("hyper-upscale", cw, ch);
        if (ctx.fullscreen_pass("hyper-rcas", PassStage::Effects, out.texture, ShaderId::hyper_fsr_rcas_frag,
                                {PassTexture{cur, {}, CommonSampler::NearestClamp}}, &ru, sizeof(ru)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

// =============================================================================
// Hyper Clean
// =============================================================================
class HyperClean final : public Effect {
public:
    enum : u32 { kDenoise = 0, kDeblock, kDeinterlace, kFrames, kPath, kCount };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{hyper::effect_keys::kHyperClean, "Hyper Clean", "Hyper", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kDeint[] = {"Desligado", "Automático", "Sempre"};
        p.add_float("denoise", "Reduzir ruído", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("deblock", "Tirar blocos de compressão", false, kParamNone);
        p.add_enum("deinterlace", "Desentrelaçar", kDeint, 3, 0);
        p.add_int("frames", "Quadros vizinhos", 2, 1, 2, kParamNone);
        p.add_enum("path", "Caminho", kPathLabels, 3, 0);
        p.set_tip(kFrames, "Quadros de cada lado alinhados pelo movimento (1 = 3 quadros, 2 = 5 quadros).");
        p.set_tip(kDeinterlace, "Automático só age quando o quadro tem o pente do vídeo entrelaçado.");
        p.set_tip(kPath, "A prévia usa sempre o Turbo. Lite e Max (redes) valem no job Hyper da Home.");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        if (v.size() < kCount) return false;
        v[kDenoise] = ParamValue::scalar(80.0f);
        v[kDeblock] = ParamValue::scalar(1.0f);
        return true;
    }
    static f32 denoise_of(const EffectEval& e) noexcept {
        return std::clamp(finite_or(e.f(kDenoise), 0.0f) * 0.01f, 0.0f, 1.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < kCount) return true;
        return denoise_of(e) <= 1e-4f && !e.b(kDeblock) && e.e(kDeinterlace) == 0;
    }
    f32 input_margin(const EffectEval&) const noexcept override { return 8.0f; }
    bool wants_neighbors() const noexcept override { return true; }
    u32 temporal_neighbors(const EffectEval& e, i32* offsets) const noexcept override {
        if (!offsets || e.count < kCount || denoise_of(e) <= 1e-4f) return 0;
        const i32 n = std::clamp(e.value(kFrames).as_int(), 1, 2);
        u32 count = 0;
        for (i32 k = 1; k <= n; ++k) {
            offsets[count++] = -k;
            offsets[count++] = k;
        }
        return count;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        for (ShaderId id : {ShaderId::hyper_deint_score_frag, ShaderId::hyper_deint_reduce_frag, ShaderId::hyper_deint_frag,
                            ShaderId::hyper_deblock_frag, ShaderId::hyper_me_luma_frag, ShaderId::effects_downsample_frag,
                            ShaderId::hyper_me_search_frag, ShaderId::hyper_me_median_frag, ShaderId::hyper_dn_accum_frag,
                            ShaderId::hyper_dn_finish_frag, ShaderId::hyper_from_perc_frag})
            out.push_back(PipelineKey::fullscreen(id, work));
    }

    /// Desentrelaça e desbloqueia uma imagem (o quadro ou um vizinho).
    /// `perc` sai verdadeiro quando o resultado ficou perceptual.
    static Status preprocess(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, FGTexture src,
                             FGTexture score, FGTexture& out, bool& perc) noexcept {
        const u32 W = input.width, H = input.height;
        out = src;
        perc = false;
        const u32 deint = e.e(kDeinterlace);
        if (deint != 0 && score.valid()) {
            const TwoU u{size_vec(W, H), Vec4{deint >= 2 ? 2.0f : 1.0f, input.texel_scale_y(), 0.0f, 0}};
            const FGTexture next = ctx.texture("hyper-desentrelaca", W, H);
            if (ctx.fullscreen_pass("hyper-desentrelaca", PassStage::Effects, next, ShaderId::hyper_deint_frag,
                                    {PassTexture{out, {}, CommonSampler::NearestClamp}, PassTexture{score, {}, CommonSampler::NearestClamp}},
                                    &u, sizeof(u)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            out = next;
        }
        if (e.b(kDeblock)) {
            for (u32 axis = 0; axis < 2; ++axis) {
                const ThreeU u{size_vec(W, H),
                               Vec4{axis == 0 ? input.texel_scale_x() : input.texel_scale_y(),
                                    axis == 0 ? input.region.x : input.region.y, static_cast<f32>(axis), perc ? 0.0f : 1.0f},
                               Vec4{0.6f, 0, 0, 0}};
                const FGTexture next = ctx.texture("hyper-blocos", W, H);
                if (ctx.fullscreen_pass("hyper-blocos", PassStage::Effects, next, ShaderId::hyper_deblock_frag,
                                        {PassTexture{out, {}, CommonSampler::NearestClamp}}, &u, sizeof(u)) == kInvalidIndex)
                    return Errc::PipelineCompileFailed;
                out = next;
                perc = true;
            }
        }
        return OkStatus;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        if (!input.valid() || e.count < kCount) return Errc::InvalidArgument;
        const u32 W = input.width, H = input.height;
        const f32 denoise = denoise_of(e);
        // Pontuação de pente do quadro (Automático) — também vale para os vizinhos.
        FGTexture score{};
        const u32 deint = e.e(kDeinterlace);
        if (deint != 0) {
            u32 tw = std::max(1u, (W + 7) / 8), th = std::max(1u, (H + 7) / 8);
            const SizeU su{size_vec(W, H)};
            FGTexture s = ctx.texture("hyper-pente", tw, th);
            if (ctx.fullscreen_pass("hyper-pente", PassStage::Effects, s, ShaderId::hyper_deint_score_frag,
                                    {PassTexture{input.texture, {}, CommonSampler::NearestClamp}}, &su, sizeof(su)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            for (u32 guard = 0; (tw > 1 || th > 1) && guard < 12; ++guard) {
                const u32 nw = std::max(1u, (tw + 3) / 4), nh = std::max(1u, (th + 3) / 4);
                const SizeU ru{size_vec(tw, th)};
                const FGTexture next = ctx.texture("hyper-pente", nw, nh);
                if (ctx.fullscreen_pass("hyper-pente-reduz", PassStage::Effects, next, ShaderId::hyper_deint_reduce_frag,
                                        {PassTexture{s, {}, CommonSampler::NearestClamp}}, &ru, sizeof(ru)) == kInvalidIndex)
                    return Errc::PipelineCompileFailed;
                s = next;
                tw = nw;
                th = nh;
            }
            score = s;
        }
        FGTexture cur{};
        bool curPerc = false;
        if (const Status s = preprocess(ctx, e, input, input.texture, score, cur, curPerc); !s.ok()) return s;

        out = input;
        out.history = FGTexture{};
        out.neighbors = TemporalNeighbors{};
        if (denoise > 1e-4f) {
            // Vizinhos na MESMA área e densidade, do mais perto para o mais longe.
            EffectBuildContext::NeighborImage order[kMaxTemporalNeighbors];
            u32 count = 0;
            for (const EffectBuildContext::NeighborImage& nb : ctx.neighbors()) {
                if (count >= kMaxTemporalNeighbors || !nb.image.valid() || nb.offset == 0 || std::abs(nb.offset) > 2) continue;
                if (nb.image.width != W || nb.image.height != H
                    || std::fabs(nb.image.region.x - input.region.x) > 0.5f || std::fabs(nb.image.region.y - input.region.y) > 0.5f
                    || std::fabs(nb.image.region.w - input.region.w) > 0.5f || std::fabs(nb.image.region.h - input.region.h) > 0.5f)
                    continue;
                order[count++] = nb;
            }
            std::stable_sort(order, order + count, [](const auto& a, const auto& b) { return std::abs(a.offset) < std::abs(b.offset); });
            u32 fw = 0, fh = 0;
            hyper::fx::field_size(W, H, fw, fh);
            const f32 h = 0.006f + 0.05f * denoise;
            FGTexture acc = cur;
            f32 accMode = curPerc ? 2.0f : 1.0f;   // 0 acumulado, 1 linear, 2 perceptual
            for (u32 n = 0; n < count; ++n) {
                FGTexture nbr{};
                bool nbrPerc = false;
                if (const Status s = preprocess(ctx, e, input, order[n].image.texture, score, nbr, nbrPerc); !s.ok()) return s;
                const FGTexture luma = hyper::fx::luma_pair(ctx, cur, nbr, W, H, curPerc, nbrPerc, PassStage::Effects);
                const FGTexture field = hyper::fx::motion_field(ctx, luma, W, H, false, PassStage::Effects);
                if (!field.valid()) continue;
                const f32 distance = 1.0f / (1.0f + 0.3f * static_cast<f32>(std::abs(order[n].offset) - 1));
                const ThreeU u{size_vec(W, H), Vec4{static_cast<f32>(hyper::fx::kBlock), static_cast<f32>(fw), static_cast<f32>(fh), 0},
                               Vec4{h, accMode, distance, nbrPerc ? 1.0f : 0.0f}};
                const FGTexture next = ctx.texture("hyper-tempo", W, H);
                if (ctx.fullscreen_pass("hyper-tempo", PassStage::Effects, next, ShaderId::hyper_dn_accum_frag,
                                        {PassTexture{acc, {}, CommonSampler::NearestClamp}, PassTexture{nbr, {}, CommonSampler::LinearClamp},
                                         PassTexture{field, {}, CommonSampler::LinearClamp}, PassTexture{luma, {}, CommonSampler::LinearClamp}},
                                        &u, sizeof(u)) == kInvalidIndex)
                    return Errc::PipelineCompileFailed;
                acc = next;
                accMode = 0.0f;
            }
            const TwoU fu{size_vec(W, H), Vec4{0.004f + 0.03f * denoise, accMode, 1.0f, 0}};
            out.texture = ctx.texture("hyper-clean", W, H);
            if (ctx.fullscreen_pass("hyper-limpo", PassStage::Effects, out.texture, ShaderId::hyper_dn_finish_frag,
                                    {PassTexture{acc, {}, CommonSampler::NearestClamp}, PassTexture{input.texture, {}, CommonSampler::NearestClamp}},
                                    &fu, sizeof(fu)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            return OkStatus;
        }
        if (curPerc) {
            out.texture = ctx.texture("hyper-clean", W, H);
            if (ctx.fullscreen_pass("hyper-volta", PassStage::Effects, out.texture, ShaderId::hyper_from_perc_frag,
                                    {PassTexture{cur, {}, CommonSampler::NearestClamp}}, nullptr, 0) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            return OkStatus;
        }
        out.texture = cur;
        return OkStatus;
    }
};

// =============================================================================
// Hyper Motion (declarativo: o renderer interpola na fonte)
// =============================================================================
class HyperMotion final : public Effect {
public:
    enum : u32 { kSlowMotion = 0, kDuplicates, kPath, kCount };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{hyper::effect_keys::kHyperMotion, "Hyper Motion", "Hyper", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kSlow[] = {"1× (só quadros a mais)", "2×", "4×", "8×"};
        p.add_enum("slow_motion", "Câmera lenta", kSlow, 4, 0);
        p.add_bool("duplicates", "Detectar quadros repetidos", true, kParamNone);
        p.add_enum("path", "Caminho", kPathLabels, 3, 0);
        p.set_tip(kSlowMotion, "Quadros novos entre os da fonte pelo movimento (vetores de bloco nos dois sentidos). "
                               "1×: a camada fica com o fps da composição (ex.: vídeo 30 numa composição 60). "
                               "A câmera lenta anda dentro da camada; o som da camada não muda.");
        p.set_tip(kPath, "A prévia usa sempre o Turbo.");
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        if (v.size() < kCount) return false;
        v[kSlowMotion] = ParamValue::scalar(1.0f);   // a prévia do catálogo mostra a câmera lenta x2
        return true;
    }
};

} // namespace

void register_hyper_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<HyperUpscale>());
    (void)r.add(std::make_unique<HyperClean>());
    (void)r.add(std::make_unique<HyperMotion>());
}

} // namespace aurea::builtin
