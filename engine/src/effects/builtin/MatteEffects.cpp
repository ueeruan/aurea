// =============================================================================
//  Recorte pelo pixel — Contorno da silhueta · Refinar recorte.
//
//  Os dois trabalham sobre o ALFA de qualquer camada (vídeo recortado por
//  croma, PNG, pré-composição): o traço de forma e de texto continua sendo o
//  deles, este é o do pixel.
//
//  Contorno: campo de distância euclidiano da silhueta em DOIS passes
//  separáveis — o horizontal guarda, por pixel, a distância na linha até o
//  pixel opaco (e até o transparente) mais próximo; o vertical fecha a conta
//  com min(dx² + dy²) e pinta a faixa. Largura grande: o campo é calculado
//  numa versão reduzida (um campo de distância interpola bem), o custo por
//  pixel fica preso.
//
//  Refinar recorte: encolher/crescer a máscara (morfológico do Minimax, pelo
//  alfa) e suavizar a borda (gaussiano) — o que uma chave de croma por pixel
//  não pode fazer sem perder a fusão com a correção de cor.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

Vec4 region_vec(const Rect& r) noexcept { return Vec4{r.x, r.y, r.w, r.h}; }

/// Fator de redução para o alcance `reach` (texels) caber em `limit` taps.
u32 reduction_for_reach(f32 reach, f32 limit) noexcept {
    return static_cast<u32>(std::clamp(std::ceil(reach / limit), 1.0f, 64.0f));
}

// -----------------------------------------------------------------------------
// Contorno da silhueta
// -----------------------------------------------------------------------------
class StrokeOutline final : public Effect {
public:
    enum : u32 { kWidth = 0, kColor, kSoftness, kPosition, kOpacity };
    static constexpr f32 kMaxTaps = 48.0f;   ///< laço fixo do shader, por passe

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kStrokeOutline, "Contorno da silhueta", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kPositions[] = {"Fora", "Centro", "Dentro"};
        // O campo de distância reduz a imagem enquanto o alcance passar de 48
        // texels: a faixa digitada vai a 1000 px sem estourar o custo.
        p.add_float("width", "Largura", 4.0f, 0.0f, 100.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 1000.0f);
        p.add_color("color", "Cor", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("softness", "Suavidade", 0.0f, 0.0f, 50.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 500.0f);
        p.add_enum("position", "Posição", kPositions, 3, 0);
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // Por dentro: a prévia do catálogo é uma foto que enche o quadro, e um
        // traço por fora dela cairia fora da tela.
        v[kWidth] = ParamValue::scalar(6.0f);
        v[kColor] = ParamValue::color(1.0f, 0.55f, 0.05f, 1.0f);
        v[kPosition] = ParamValue::scalar(2.0f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kWidth), 0.0f) > 0.01f) || !(finite_or(e.f(kOpacity), 0.0f) > 0.01f);
    }
    /// Até onde o traço chega fora da silhueta (px): a largura que sai, mais
    /// a suavidade, mais um pixel de antialiasing.
    static f32 reach(const EffectEval& e) noexcept {
        const f32 w = std::clamp(finite_or(e.f(kWidth), 0.0f), 0.0f, 1000.0f);
        const f32 soft = std::clamp(finite_or(e.f(kSoftness), 0.0f), 0.0f, 500.0f);
        const u32 pos = e.e(kPosition);
        const f32 outward = pos == 0 ? w : (pos == 1 ? w * 0.5f : 0.0f);
        return outward + soft + 1.0f;
    }
    /// A distância precisa ser medida até onde o traço alcança dos dois lados.
    static f32 search(const EffectEval& e) noexcept {
        const f32 w = std::clamp(finite_or(e.f(kWidth), 0.0f), 0.0f, 1000.0f);
        const f32 soft = std::clamp(finite_or(e.f(kSoftness), 0.0f), 0.0f, 500.0f);
        return w + soft + 1.0f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return search(e); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_stroke_distance_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_stroke_outline_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 width = std::clamp(finite_or(e.f(kWidth), 0.0f), 0.0f, 1000.0f);
        const f32 soft = std::clamp(finite_or(e.f(kSoftness), 0.0f), 0.0f, 500.0f);
        const f32 grow = reach(e);
        const f32 look = search(e);
        const Rect region = spread_region(input.region, grow, grow, e.placement, margin);
        const f32 scale = input.texel_scale_x();
        // Passe 1 numa escala reduzida: o alcance em texels cabe no laço.
        const u32 k = reduction_for_reach(look * scale, kMaxTaps);
        const f32 distScale = scale / static_cast<f32>(k);
        u32 dw = 0, dh = 0;
        ctx.region_size(region, distScale, dw, dh);
        const f32 pxPerDistTexel = (dw > 0 && region.w > 0.0f) ? region.w / static_cast<f32>(dw) : 1.0f;
        const f32 taps = std::clamp(std::ceil(look / pxPerDistTexel), 1.0f, kMaxTaps);

        EffectUniforms u1 = base_uniforms(input);
        u1.uvMap = EffectBuildContext::uv_map(region, input.region);
        u1.p0 = Vec4{taps, pxPerDistTexel, look, 0.0f};
        u1.p2 = region_vec(input.region);
        u1.p3 = region_vec(region);
        const FGTexture dist = ctx.texture("contorno-distancia", dw, dh);
        if (ctx.fullscreen_pass("contorno-distancia", PassStage::Effects, dist, ShaderId::effects_stroke_distance_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u1, sizeof(u1)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }

        // Passe 2 na escala da camada: fecha a distância e pinta.
        u32 w = 0, h = 0;
        ctx.region_size(region, scale, w, h);
        EffectUniforms u2 = base_uniforms(input);
        u2.uvMap = EffectBuildContext::uv_map(region, input.region);
        u2.texel = Vec4{dw > 0 ? 1.0f / static_cast<f32>(dw) : 0.0f, dh > 0 ? 1.0f / static_cast<f32>(dh) : 0.0f,
                        (w > 0 && region.w > 0.0f) ? static_cast<f32>(w) / region.w : 1.0f,
                        (h > 0 && region.h > 0.0f) ? static_cast<f32>(h) / region.h : 1.0f};
        u2.p0 = Vec4{taps, pxPerDistTexel, width, soft};
        u2.p1 = Vec4{static_cast<f32>(e.e(kPosition)),
                     std::clamp(finite_or(e.f(kOpacity), 100.0f) / 100.0f, 0.0f, 1.0f), look, 0.0f};
        u2.p2 = region_vec(input.region);
        u2.p3 = region_vec(region);
        u2.color = e.color(kColor);
        out = LayerImage{ctx.texture("contorno-silhueta", w, h), region, w, h};
        if (ctx.fullscreen_pass("contorno-silhueta", PassStage::Effects, out.texture, ShaderId::effects_stroke_outline_frag,
                                {PassTexture{dist, {}, CommonSampler::LinearClamp},
                                 PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u2, sizeof(u2)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Refinar recorte
// -----------------------------------------------------------------------------
class MatteRefine final : public Effect {
public:
    enum : u32 { kChoke = 0, kFeather, kShowMatte };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kMatteRefine, "Refinar recorte", "Recorte", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // Encolher positivo come a borda (o halo do fundo verde); negativo
        // cresce. O morfológico reduz a imagem além de 6 texels: digitado longe.
        p.add_float("choke", "Encolher", 0.0f, -50.0f, 50.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(-500.0f, 500.0f);
        p.add_float("feather", "Suavizar borda", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 1000.0f);
        p.add_bool("show_matte", "Mostrar máscara", false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kChoke] = ParamValue::scalar(6.0f);
        v[kFeather] = ParamValue::scalar(4.0f);
        return true;
    }
    static f32 choke(const EffectEval& e) noexcept { return std::clamp(finite_or(e.f(kChoke), 0.0f), -500.0f, 500.0f); }
    static f32 feather(const EffectEval& e) noexcept { return std::clamp(finite_or(e.f(kFeather), 0.0f), 0.0f, 1000.0f); }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(choke(e)) < 0.5f && feather(e) < 0.05f && !e.b(kShowMatte);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::fabs(choke(e)) + feather(e) * 1.5f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_matte_choke_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_matte_view_frag, work));
    }
    /// Encolher/crescer pelo alfa: morfológico redondo (matte_choke.frag),
    /// numa escala em que o raio cabe nos ±6 texels do núcleo.
    static Status morph(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 radius,
                        bool dilate, f32 margin, LayerImage& out) {
        const u32 k = reduction_for_reach(radius * input.texel_scale_x(), 6.0f);
        const Rect region = dilate ? spread_region(input.region, radius, radius, e.placement, margin) : input.region;
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x() / static_cast<f32>(k), w, h);
        // A textura de entrada mantém a densidade dela: o passo é em texels
        // DELA, e o raio também.
        const f32 texelsX = input.texel_scale_x();
        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{input.width > 0 ? 1.0f / static_cast<f32>(input.width) : 0.0f,
                       input.height > 0 ? 1.0f / static_cast<f32>(input.height) : 0.0f, texelsX, texelsX};
        // Raio em texels da ENTRADA, no máximo 6 (reduzido, o passo cresce).
        const f32 stepTexels = static_cast<f32>(k);
        u.texel.x *= stepTexels;
        u.texel.y *= stepTexels;
        u.p0 = Vec4{std::min(radius * texelsX / stepTexels, 6.0f), dilate ? 1.0f : 0.0f, 0.0f, 0.0f};
        out = LayerImage{ctx.texture("refinar-recorte", w, h), region, w, h};
        if (ctx.fullscreen_pass("refinar-recorte", PassStage::Effects, out.texture, ShaderId::effects_matte_choke_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        LayerImage cur = input;
        const f32 c = choke(e);
        if (std::fabs(c) >= 0.5f) {
            LayerImage next;
            if (const Status s = morph(ctx, e, cur, std::fabs(c), c < 0.0f, margin, next); !s.ok()) return s;
            cur = next;
        }
        const f32 f = feather(e);
        if (f >= 0.05f) {
            BlurRequest req;
            req.sigmaX = req.sigmaY = f * 0.5f;
            req.outRegion = spread_region(cur.region, f * 1.5f, f * 1.5f, e.placement, margin);
            req.label = "matte";
            LayerImage next;
            if (const Status s = build_gaussian(ctx, cur, req, next); !s.ok()) return s;
            cur = next;
        }
        if (!e.b(kShowMatte)) {
            out = cur;
            return OkStatus;
        }
        EffectUniforms u = base_uniforms(cur);
        return single_pass(ctx, ShaderId::effects_matte_view_frag, cur, u, "mostrar-mascara", out);
    }
};

// Two rounds of choke and softness, matching the editable matte workflow.
class MatteChoker final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.key.matte_choker","Matte Choker","Recorte",EffectClass::Neighborhood}; return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("choke1","Encolher 1",2,-50,50,kParamAnimatable|kParamPixels,"px");
        p.add_float("softness1","Suavidade 1",1,0,100,kParamAnimatable|kParamPixels,"px");
        p.add_float("choke2","Encolher 2",0,-50,50,kParamAnimatable|kParamPixels,"px");
        p.add_float("softness2","Suavidade 2",0,0,100,kParamAnimatable|kParamPixels,"px");
        p.add_int("iterations","Iterações",1,1,4);
        p.add_bool("show_matte","Mostrar máscara",false);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return (std::abs(e.f(0))+std::abs(e.f(2))+1.5f*(e.f(1)+e.f(3)))*std::clamp(e.value(4).as_int(),1,4);
    }
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat f) const override { MatteRefine{}.pipelines(out,f); }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32 margin,LayerImage& out) const override {
        LayerImage current=input;
        for(int n=0;n<std::clamp(e.value(4).as_int(),1,4);++n) for(u32 step=0;step<2;++step) {
            ParamValue values[]={e.value(step*2),e.value(step*2+1),ParamValue::scalar(0)};
            EffectEval pass=e; pass.values=values; pass.count=3;
            LayerImage next;
            if(const Status s=MatteRefine{}.build(ctx,pass,current,margin,next);!s.ok())return s;
            current=next;
        }
        if(e.b(5))return single_pass(ctx,ShaderId::effects_matte_view_frag,current,base_uniforms(current),"matte-choker-view",out);
        out=current; return OkStatus;
    }
};
} // namespace

void register_matte_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<StrokeOutline>());
    (void)r.add(std::make_unique<MatteRefine>());
}

void register_matte_choker(EffectRegistry& r) {
    (void)r.add(std::make_unique<MatteChoker>());
}

} // namespace aurea::builtin
