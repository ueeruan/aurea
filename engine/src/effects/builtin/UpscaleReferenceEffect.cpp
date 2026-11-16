// Ampliação com preservação de detalhes (referência do AE, ADBE Upscale):
// reconstrução independente do comportamento observado no Upscale.aex do
// After Effects 2027. Evidência e números em
// build/reference/effects-20261115/batch-a2/upscale.md (modelo numpy em
// batch-a2/upscale_model.py, medido contra os quadros do AE).
//
// O que o efeito faz:
//  - a escala vira passos de 1,5x (no máximo 5; limites 1,15 / 1,25 / 1,35 /
//    1,55 no fator que falta) e uma reamostragem final para o tamanho exato;
//  - cada passo: L1 = a entrada ampliada 1,5x (cúbico a = -0.625 esticado
//    1,15x, centrado, posições em 16.16); L0 = a entrada com um gaussiano
//    leve; numa grade de remendos 5x5 (passo 3, 2 ou 1 conforme o fator que
//    falta) cada centro de L1 procura, entre o pixel correspondente da entrada
//    e os 4 vizinhos, o remendo de L0 mais parecido; L1 recebe a média do
//    detalhe (entrada - L0) dos remendos que cobrem cada pixel;
//  - a cor é processada reta (sem alfa) e o alfa, no modo Bicúbico (padrão),
//    é um cúbico a = -0.75 direto para o tamanho final;
//  - 100% devolve a entrada sem tocar; "Detalhe" não altera a imagem no AE
//    2027 (o valor não chega ao núcleo) e aqui também não.
// "Reduzir ruído" (um passo de remoção de ruído na escala 1) ainda não foi
// reconstruído: o controle existe para a importação do AE e não altera nada.
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace aurea::builtin {
namespace {

constexpr const char* kUpscaleReference = "aurea.distort.upscale_reference";
constexpr f32 kLowPassSigma = 0.54f;   // medido contra o quadro do AE (batch-a2/upscale.md)
constexpr i32 kMargin = 16;            // borda de L1 sem detalhe (16 px; o primeiro pixel é 17)

f64 finite_value(f32 value, f64 fallback) noexcept { return std::isfinite(value) ? static_cast<f64>(value) : fallback; }

/// Passos de 1,5x que o efeito nativo dá para a escala pedida (fator > 1).
u32 upscale_steps(f64 scale) noexcept {
    u32 i = 0;
    while (i < 5 && scale >= 1.15 && (scale >= 1.25 || i < 1) && (scale >= 1.35 || i < 2) && (scale >= 1.55 || i < 3)) {
        ++i;
        scale /= 1.5;
    }
    return i;
}

/// Novo tamanho num passo: round(n·1,5), mantendo a diferença par (cresce
/// igual dos dois lados).
i32 grow(i32 n) noexcept {
    const i32 m = static_cast<i32>(n * 1.5 + 0.5);
    return ((m - n) & 1) ? m + 1 : m;
}

i32 grid_stride(f64 remaining) noexcept {
    i32 stride = 1;
    if (remaining < 3.71) stride = 2;
    if (remaining < 2.48) stride = 3;
    return stride;
}

/// Posição inicial (16.16) do reamostrador centrado do nativo.
i32 resample_origin(i32 src, i32 dst, i32 step) noexcept {
    i64 pos = (src % 2 == 0) ? -(static_cast<i64>(dst / 2 - 1) * step) - step / 2
                             : -((static_cast<i64>(dst - 1) * step) / 2);
    pos += (static_cast<i64>(src) * 65536 - 65536) / 2;
    return static_cast<i32>(pos);
}

i32 resample_step(f64 scale) noexcept {
    const f64 v = 65536.0 / scale;
    return static_cast<i32>(v >= 0 ? v + 0.5 : v - 0.5);
}

struct ResampleUniforms { i32 sizes[4]; i32 axis[4]; };
struct BlurUniforms { i32 size[4]; Vec4 sigma; };
struct MatchUniforms { i32 upSize[4]; i32 srcSize[4]; };
struct DetailUniforms { i32 upSize[4]; i32 srcSize[4]; i32 inner[4]; };
struct CombineUniforms { i32 mode[4]; };

class UpscaleReference final : public Effect {
public:
    enum : u32 { kScale = 0, kReduceNoise, kDetail, kAlpha };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kUpscaleReference, "Ampliação com preservação de detalhes", "Distorcer",
                                  EffectClass::Neighborhood};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("scale", "Escala", 100.0f, 100.0f, 400.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(100.0f, 30000.0f);
        p.add_float("reduce_noise", "Reduzir ruído", 0.0f, 0.0f, 100.0f, kParamAnimatable);
        p.add_float("detail", "Detalhe", 50.0f, 0.0f, 100.0f, kParamAnimatable);
        static const char* const alpha[] = {"Preservar detalhes", "Bicúbico"};
        p.add_enum("alpha", "Alfa", alpha, 2, 1);
        p.set_tip(kDetail, "No After Effects 2027 este valor não muda a imagem; fica para projetos importados.");
        p.set_tip(kReduceNoise, "Ainda não reproduzido; fica para projetos importados.");
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        values[kScale] = ParamValue::scalar(200);
        return true;
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        return !(std::clamp(finite_value(e.f(kScale), 100), 100., 30000.) > 100.0);
    }

    bool needs_full_input() const noexcept override { return true; }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override {
        for (ShaderId id : {ShaderId::effects_upscale_reference_resample_frag, ShaderId::effects_upscale_reference_blur_frag,
                            ShaderId::effects_upscale_reference_match_frag, ShaderId::effects_upscale_reference_detail_frag,
                            ShaderId::effects_upscale_reference_combine_frag})
            out.push_back(PipelineKey::fullscreen(id, format));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        out = input;
        const f64 scale = std::clamp(finite_value(e.f(kScale), 100), 100., 30000.) / 100.0;
        if (!(scale > 1.0) || !input.valid()) return OkStatus;
        const i32 w = static_cast<i32>(input.width), h = static_cast<i32>(input.height);
        const i32 dstW = static_cast<i32>(std::lround(w * scale)), dstH = static_cast<i32>(std::lround(h * scale));
        const u32 steps = upscale_steps(scale);
        // O maior buffer é o último passo de 1,5x (antes da reamostragem final).
        i32 peakW = w, peakH = h;
        for (u32 i = 0; i < steps; ++i) { peakW = grow(peakW); peakH = grow(peakH); }
        const i64 limit = static_cast<i64>(ctx.max_texture_size());
        if (std::max<i64>({dstW, dstH, peakW, peakH}) > std::min<i64>(limit, 32000))
            return Status{Errc::OutOfDeviceMemory, "Upscale: output exceeds the GPU texture extent"};

        auto pass = [&](const char* name, FGTexture target, ShaderId shader, std::initializer_list<PassTexture> textures,
                        const void* u, u32 bytes) {
            return ctx.fullscreen_pass(name, PassStage::Effects, target, shader, textures, u, bytes) != kInvalidIndex;
        };
        const auto nearest = [](FGTexture t) { return PassTexture{t, {}, CommonSampler::NearestClamp}; };

        auto resample = [&](FGTexture source, i32 sw, i32 sh, i32 tw, i32 th, f64 factor, bool detail,
                            const char* name, FGTexture& result) {
            const i32 step = resample_step(factor);
            const FGTexture mid = ctx.texture(name, static_cast<u32>(tw), static_cast<u32>(sh));
            ResampleUniforms hu{{sw, sh, tw, sh}, {0, resample_origin(sw, tw, step), step, detail ? 0 : 1}};
            if (!pass(name, mid, ShaderId::effects_upscale_reference_resample_frag, {nearest(source)}, &hu, sizeof(hu)))
                return false;
            result = ctx.texture(name, static_cast<u32>(tw), static_cast<u32>(th));
            ResampleUniforms vu{{tw, sh, tw, th}, {1, resample_origin(sh, th, step), step, detail ? 0 : 1}};
            return pass(name, result, ShaderId::effects_upscale_reference_resample_frag, {nearest(mid)}, &vu, sizeof(vu));
        };

        // Os planos do nativo: a cor reta (ou o alfa replicado) passo a passo.
        auto planes = [&](FGTexture source, FGTexture& result) {
            FGTexture cur = source;
            i32 cw = w, ch = h;
            f64 remaining = scale;
            for (u32 i = 0; i < steps; ++i) {
                const i32 nw = grow(cw), nh = grow(ch);
                FGTexture up;
                if (!resample(cur, cw, ch, nw, nh, 1.5, true, "upscale-l1", up)) return false;
                const FGTexture blurX = ctx.texture("upscale-l0", static_cast<u32>(cw), static_cast<u32>(ch));
                const FGTexture low = ctx.texture("upscale-l0", static_cast<u32>(cw), static_cast<u32>(ch));
                BlurUniforms bx{{cw, ch, 0, 4}, Vec4{kLowPassSigma, 0, 0, 0}};
                BlurUniforms by{{cw, ch, 1, 4}, Vec4{kLowPassSigma, 0, 0, 0}};
                if (!pass("upscale-l0", blurX, ShaderId::effects_upscale_reference_blur_frag, {nearest(cur)}, &bx, sizeof(bx)) ||
                    !pass("upscale-l0", low, ShaderId::effects_upscale_reference_blur_frag, {nearest(blurX)}, &by, sizeof(by)))
                    return false;
                const i32 stride = grid_stride(remaining);
                const i32 first = kMargin + 1 - 2 - 1;   // 14: o primeiro centro da grade
                const i32 lastX = nw - kMargin + 2, lastY = nh - kMargin + 2;
                const FGTexture match = ctx.texture("upscale-match", static_cast<u32>(nw), static_cast<u32>(nh));
                MatchUniforms mu{{nw, nh, stride, first}, {cw, ch, lastX, lastY}};
                if (!pass("upscale-match", match, ShaderId::effects_upscale_reference_match_frag,
                          {nearest(up), nearest(low)}, &mu, sizeof(mu)))
                    return false;
                const FGTexture detail = ctx.texture("upscale-detail", static_cast<u32>(nw), static_cast<u32>(nh));
                DetailUniforms du{{nw, nh, stride, first}, {cw, ch, lastX, lastY},
                                  {kMargin + 1, kMargin + 1, nw - kMargin, nh - kMargin}};
                if (!pass("upscale-detail", detail, ShaderId::effects_upscale_reference_detail_frag,
                          {nearest(up), nearest(cur), nearest(low), nearest(match)}, &du, sizeof(du)))
                    return false;
                cur = detail;
                cw = nw;
                ch = nh;
                remaining /= 1.5;
            }
            return resample(cur, cw, ch, dstW, dstH, remaining, true, "upscale-final", result);
        };

        const FGTexture straight = ctx.texture("upscale-straight", static_cast<u32>(w), static_cast<u32>(h));
        CombineUniforms prep{{0, w, h, 0}};
        if (!pass("upscale-straight", straight, ShaderId::effects_upscale_reference_combine_frag,
                  {nearest(input.texture), nearest(input.texture)}, &prep, sizeof(prep)))
            return Errc::PipelineCompileFailed;
        FGTexture color;
        if (!planes(straight, color)) return Errc::PipelineCompileFailed;
        FGTexture alpha;
        i32 alphaChannel = 0;
        if (e.e(kAlpha) == 0) {
            const FGTexture replicated = ctx.texture("upscale-alpha", static_cast<u32>(w), static_cast<u32>(h));
            CombineUniforms rep{{2, w, h, 0}};
            if (!pass("upscale-alpha", replicated, ShaderId::effects_upscale_reference_combine_frag,
                      {nearest(straight), nearest(straight)}, &rep, sizeof(rep)) ||
                !planes(replicated, alpha))
                return Errc::PipelineCompileFailed;
            alphaChannel = 1;
        } else if (!resample(straight, w, h, dstW, dstH, scale, false, "upscale-alpha", alpha)) {
            return Errc::PipelineCompileFailed;
        }
        // A saída cresce em volta do centro da entrada.
        const f32 regionW = static_cast<f32>(dstW) / std::max(input.texel_scale_x(), 1e-6f);
        const f32 regionH = static_cast<f32>(dstH) / std::max(input.texel_scale_y(), 1e-6f);
        const Rect region{input.region.x + input.region.w * .5f - regionW * .5f,
                          input.region.y + input.region.h * .5f - regionH * .5f, regionW, regionH};
        const FGTexture target = ctx.texture("upscale-reference", static_cast<u32>(dstW), static_cast<u32>(dstH));
        CombineUniforms fin{{1, dstW, dstH, alphaChannel}};
        if (!pass("upscale-reference", target, ShaderId::effects_upscale_reference_combine_frag,
                  {nearest(color), nearest(alpha)}, &fin, sizeof(fin)))
            return Errc::PipelineCompileFailed;
        out = LayerImage{target, region, static_cast<u32>(dstW), static_cast<u32>(dstH)};
        return OkStatus;
    }
};

} // namespace

void register_upscale_reference(EffectRegistry& registry) {
    (void)registry.add(std::make_unique<UpscaleReference>());
}

} // namespace aurea::builtin
