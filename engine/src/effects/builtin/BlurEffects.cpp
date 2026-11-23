// =============================================================================
//  Gaussian Blur e Sharpen — efeitos de vizinhança.
//
//  O gaussiano é separável (H depois V) e com PIRÂMIDE: enquanto o sigma em
//  texels passar de 8, a imagem é reduzida por 2 (caixa 4x4) e o sigma cai
//  pela metade. O custo por pixel fica limitado (≤ 2x24 pares de amostras) para
//  qualquer raio, e um blur de raio 300 num 4K roda em frações de milissegundo.
//  A saída fica na resolução reduzida: a composição a lê com filtro bilinear,
//  o que para uma imagem já borrada por sigma ≥ 4 texels não tem perda visível
//  — e poupa o passe de ampliação.
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

constexpr u32 kMaxPairs = 24;
constexpr f32 kMaxSigmaTexels = 8.0f;
constexpr u32 kMaxReductions = 7;

struct BlurUniforms {
    Vec4 uvMap;
    Vec4 dir;
    Vec4 header;
    Vec4 pairs[kMaxPairs / 2];
};
static_assert(sizeof(BlurUniforms) == 16 * 3 + 16 * (kMaxPairs / 2), "std140 de gaussian_blur.frag");

struct DownsampleUniforms {
    Vec4 uvMap;
    Vec4 texel;
};

/// Pesos do gaussiano em PARES (amostragem bilinear), normalizados.
void gaussian_pairs(f32 sigma, BlurUniforms& u, BlurKernel kernel = BlurKernel::Gaussian) noexcept {
    const u32 radius = std::min<u32>(kMaxPairs * 2, std::max<u32>(1, static_cast<u32>(std::ceil(3.0f * sigma))));
    f32 w[kMaxPairs * 2 + 2]{};
    const f32 inv2s2 = 1.0f / (2.0f * sigma * sigma);
    f32 sum = 0.0f;
    for (u32 i = 0; i <= radius; ++i) {
        switch (kernel) {
            case BlurKernel::Gaussian: w[i] = std::exp(-static_cast<f32>(i * i) * inv2s2); break;
            case BlurKernel::Box: w[i] = std::clamp(3.0f * sigma + 0.5f - static_cast<f32>(i), 0.0f, 1.0f); break;
            case BlurKernel::Triangle: w[i] = std::max(0.0f, 1.0f - static_cast<f32>(i) / (3.0f * sigma)); break;
        }
        sum += (i == 0) ? w[i] : 2.0f * w[i];
    }
    for (u32 i = 0; i <= radius; ++i) w[i] /= sum;

    u.header = Vec4{w[0], 0.0f, 0.0f, 0.0f};
    u32 pairs = 0;
    for (u32 i = 1; i <= radius && pairs < kMaxPairs; i += 2) {
        const f32 wa = w[i];
        const f32 wb = (i + 1 <= radius) ? w[i + 1] : 0.0f;
        const f32 weight = wa + wb;
        const f32 offset = weight > 0.0f ? (static_cast<f32>(i) * wa + static_cast<f32>(i + 1) * wb) / weight
                                         : static_cast<f32>(i);
        Vec4& slot = u.pairs[pairs / 2];
        if (pairs % 2 == 0) { slot.x = offset; slot.y = weight; }
        else                { slot.z = offset; slot.w = weight; }
        ++pairs;
    }
    u.header.y = static_cast<f32>(pairs);
}

} // namespace

Rect spread_region(const Rect& input, f32 extendX, f32 extendY, const LayerPlacement* placement,
                   f32 margin) noexcept {
    Rect r{input.x - extendX, input.y - extendY, input.w + 2.0f * extendX, input.h + 2.0f * extendY};
    // Bound the working surface by the viewport preimage, including camera
    // projection for 3D planes. The downstream sampling margin stays available.
    if (placement && placement->compWidth && placement->compHeight) {
        Rect vis = visible_layer_rect(*placement);
        if (vis.w > 0.0f && vis.h > 0.0f) {
            vis = Rect{vis.x - margin, vis.y - margin, vis.w + 2.0f * margin, vis.h + 2.0f * margin};
            const Rect clipped = Rect::intersect(r, vis);
            if (clipped.w > 0.5f && clipped.h > 0.5f) r = clipped;
            else r = Rect{input.x, input.y, 1.0f, 1.0f};   // nada visível: mínimo válido
        }
    }
    return r;
}

static Status build_paired_blur(EffectBuildContext& ctx, const LayerImage& input, const BlurRequest& req,
                                CommonSampler sampler, BlurKernel kernel, LayerImage& out) {
    if (!input.valid()) return Errc::InvalidArgument;
    const f32 kx = input.texel_scale_x();
    const f32 ky = input.texel_scale_y();
    f32 sx = req.sigmaX * kx;
    f32 sy = req.sigmaY * ky;
    if (sx < 0.05f && sy < 0.05f) { out = input; return OkStatus; }

    // Nomes de passe são strings estáticas (as medições de GPU voltam frames
    // depois). O blur interno do Glow ganha nome próprio para o painel
    // separar o custo dos dois efeitos.
    const bool glow = req.label && req.label[0] == 'g';
    const char* nameReduce = glow ? "glow-reducao" : "blur-reducao";
    const char* nameH = glow ? "glow-blur-h" : "blur-h";
    const char* nameV = glow ? "glow-blur-v" : "blur-v";

    const Rect region = req.outRegion.w > 0.0f ? req.outRegion : input.region;

    // ÁREA DE TRABALHO: a saída pode ter sido RECORTADA ao quadro visível, mas
    // o passe vertical lê 3σ acima e abaixo de cada pixel da saída (e o
    // horizontal, 3σ dos lados). Se a redução e o passe horizontal cobrissem só
    // a saída, a borda do recorte lia transparente — uma faixa escura no topo
    // do quadro quando a camada (ou a parede do Motion Tile) passa dele.
    // Trabalha-se na saída + 3σ; só o ÚLTIMO passe
    // escreve exatamente a região pedida.
    Rect work = region;
    u32 w = 0, h = 0;
    ctx.region_size(region, kx, w, h);
    u32 outW = w, outH = h;
    if (req.clippedRegion && !req.repeatEdges && w > 0 && h > 0) {
        // A área extra é um número INTEIRO de texels da saída: a grade da
        // saída continua a mesma (nada de refiltrar meio texel).
        const f32 tx = region.w / static_cast<f32>(w), ty = region.h / static_cast<f32>(h);
        const f32 padX = req.sigmaX > 0.0f ? std::ceil(3.0f * req.sigmaX) + 1.0f : 0.0f;
        const f32 padY = req.sigmaY > 0.0f ? std::ceil(3.0f * req.sigmaY) + 1.0f : 0.0f;
        const u32 nx = tx > 0.0f ? static_cast<u32>(std::ceil(padX / tx)) : 0u;
        const u32 ny = ty > 0.0f ? static_cast<u32>(std::ceil(padY / ty)) : 0u;
        const u32 limit = ctx.max_texture_size();
        if (w + 2 * nx <= limit && h + 2 * ny <= limit) {
            work = Rect{region.x - static_cast<f32>(nx) * tx, region.y - static_cast<f32>(ny) * ty,
                        region.w + static_cast<f32>(2 * nx) * tx, region.h + static_cast<f32>(2 * ny) * ty};
            w += 2 * nx;
            h += 2 * ny;
        }
    }

    FGTexture src = input.texture;
    Rect srcRegion = input.region;
    u32 srcW = input.width, srcH = input.height;

    const f32 maxSigma = std::clamp(req.maxSigmaTexels, 1.5f, kMaxSigmaTexels);
    for (u32 level = 0; level < kMaxReductions; ++level) {
        const bool redX = sx > maxSigma && w > 1;
        const bool redY = sy > maxSigma && h > 1;
        if (!redX && !redY) break;
        const u32 nw = redX ? std::max(1u, (w + 1) / 2) : w;
        const u32 nh = redY ? std::max(1u, (h + 1) / 2) : h;
        const FGTexture dst = ctx.texture(nameReduce, nw, nh);
        DownsampleUniforms u{};
        u.uvMap = EffectBuildContext::uv_map(work, srcRegion);
        u.texel = Vec4{redX ? 1.0f / static_cast<f32>(srcW) : 0.0f,
                       redY ? 1.0f / static_cast<f32>(srcH) : 0.0f, 0.0f, 0.0f};
        if (ctx.fullscreen_pass(nameReduce, PassStage::Effects, dst, ShaderId::effects_downsample_frag,
                                {PassTexture{src, {}, sampler}}, &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        // O sigma em texels cai na mesma razão em que a resolução caiu (a
        // região é a mesma, com menos texels).
        sx *= static_cast<f32>(nw) / static_cast<f32>(w);
        sy *= static_cast<f32>(nh) / static_cast<f32>(h);
        src = dst; srcRegion = work; srcW = nw; srcH = nh;
        w = nw; h = nh;
    }

    // Texels da região pedida na densidade atual (depois das reduções).
    if (work.w != region.w || work.h != region.h) {
        outW = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(w) * region.w / std::max(work.w, 1e-6f))));
        outH = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(h) * region.h / std::max(work.h, 1e-6f))));
    } else {
        outW = w;
        outH = h;
    }

    auto separable = [&](bool horizontal, f32 sigma, bool last) -> bool {
        const Rect dstRegion = last ? region : work;
        const u32 dw = last ? outW : w, dh = last ? outH : h;
        const FGTexture dst = ctx.texture(horizontal ? nameH : nameV, dw, dh);
        BlurUniforms u{};
        u.uvMap = EffectBuildContext::uv_map(dstRegion, srcRegion);
        u.dir = horizontal ? Vec4{1.0f / static_cast<f32>(srcW), 0.0f, 0.0f, 0.0f}
                           : Vec4{0.0f, 1.0f / static_cast<f32>(srcH), 0.0f, 0.0f};
        gaussian_pairs(sigma, u, kernel);
        // O bloco vai inteiro (240 bytes): o intervalo amarrado precisa cobrir
        // o bloco declarado no shader, e o laço lê só os pares válidos.
        if (ctx.fullscreen_pass(horizontal ? nameH : nameV, PassStage::Effects, dst,
                                ShaderId::effects_gaussian_blur_frag,
                                {PassTexture{src, {}, sampler}}, &u, sizeof(u)) == kInvalidIndex) {
            return false;
        }
        src = dst; srcRegion = dstRegion; srcW = dw; srcH = dh;
        return true;
    };

    const bool doX = sx >= 0.05f, doY = sy >= 0.05f;
    if (doX && !separable(true, sx, !doY)) return Errc::PipelineCompileFailed;
    if (doY && !separable(false, sy, true)) return Errc::PipelineCompileFailed;
    if (!doX && !doY) {
        // Só reduções (σ minúsculo depois de reduzir): a região pedida, sem filtro extra.
        BlurUniforms u{};
        u.uvMap = EffectBuildContext::uv_map(region, srcRegion);
        u.dir = Vec4{1.0f / static_cast<f32>(srcW), 0.0f, 0.0f, 0.0f};
        gaussian_pairs(0.05f, u);
        const FGTexture dst = ctx.texture(nameH, outW, outH);
        if (ctx.fullscreen_pass(nameH, PassStage::Effects, dst, ShaderId::effects_gaussian_blur_frag,
                                {PassTexture{src, {}, sampler}}, &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        src = dst; srcRegion = region; srcW = outW; srcH = outH;
    }

    out.texture = src;
    out.region = srcRegion;
    out.width = srcW;
    out.height = srcH;
    return OkStatus;
}

Status build_gaussian(EffectBuildContext& ctx, const LayerImage& input, const BlurRequest& req,
                      LayerImage& out) {
    return build_separable_blur(ctx, input, req, BlurKernel::Gaussian, out);
}

Status build_separable_blur(EffectBuildContext& ctx, const LayerImage& input, const BlurRequest& req,
                            BlurKernel kernel, LayerImage& out) {
    const CommonSampler sampler = req.repeatEdges ? CommonSampler::LinearClamp : CommonSampler::LinearBorder;
    return build_paired_blur(ctx, input, req, sampler, kernel, out);
}

namespace {

// -----------------------------------------------------------------------------
// Gaussian Blur
// -----------------------------------------------------------------------------
class GaussianBlur final : public Effect {
public:
    enum : u32 { kBlurriness = 0, kDimensions, kRepeatEdges };
    enum : u32 { kBoth = 0, kHorizontal, kVertical };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kGaussianBlur, "Desfoque gaussiano", "Desfoque",
                                  EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kDims[] = {"Horizontal e vertical", "Horizontal", "Vertical"};
        // "Desfoque" é o raio visível, em pixels da layer: sigma = raio / 3.
        p.add_float("blurriness", "Desfoque", 0.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        // Digitado até 3000 px: é o que a pirâmide cobre sem truncar o núcleo
        // (7 reduções x sigma 8 texels ≈ raio 3000). O custo por pixel não
        // cresce com o raio (≤ 2x24 pares) e a textura é presa ao quadro.
        p.typed_range(0.0f, 3000.0f);
        p.add_enum("dimensions", "Dimensões", kDims, 3, kBoth);
        p.add_bool("repeat_edges", "Repetir pixels da borda", false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kBlurriness] = ParamValue::scalar(18.0f);   // o xadrez da cartela some
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_downsample_frag, work));
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kBlurriness) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::max(0.0f, e.f(kBlurriness)); }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 radius = std::max(0.0f, e.f(kBlurriness));
        const u32 dims = e.e(kDimensions);
        const bool repeat = e.b(kRepeatEdges);
        const f32 ex = dims != kVertical ? radius : 0.0f;
        const f32 ey = dims != kHorizontal ? radius : 0.0f;

        BlurRequest req;
        req.sigmaX = ex / 3.0f;
        req.sigmaY = ey / 3.0f;
        req.repeatEdges = repeat;
        // Repetindo a borda, a imagem não "vaza" para fora da caixa; sem
        // repetir, a luz se espalha e a região cresce junto.
        req.outRegion = repeat ? input.region : spread_region(input.region, ex, ey, e.placement, margin);
        // O recorte ao quadro cortou a região espalhada: o desfoque trabalha
        // além dela (ver BlurRequest::clippedRegion).
        req.clippedRegion = !repeat && (req.outRegion.x > input.region.x - ex + 0.5f || req.outRegion.y > input.region.y - ey + 0.5f
            || req.outRegion.right() < input.region.right() + ex - 0.5f || req.outRegion.bottom() < input.region.bottom() + ey - 0.5f);
        req.label = "blur";
        return build_gaussian(ctx, input, req, out);
    }
};

// -----------------------------------------------------------------------------
// Sharpen
// -----------------------------------------------------------------------------
class Sharpen final : public Effect {
public:
    explicit Sharpen(bool reference = false) : reference_(reference) {}
    enum : u32 { kAmount = 0 };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSharpen, "Nitidez", "Desfoque", EffectClass::Neighborhood};
        static const EffectInfo reference{effect_keys::kSharpenReference, "Sharpen", "Desfoque", EffectClass::Neighborhood};
        return reference_ ? reference : i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        if (reference_) {
            p.add_int("amount", "Intensidade da nitidez", 0, 0, 4000);
            return;
        }
        p.add_float("amount", "Intensidade", 0.0f, 0.0f, 500.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 2000.0f);   // multiplicador de um núcleo 3x3: custo fixo
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmount] = ParamValue::scalar(160.0f);      // as arestas da cartela realçam
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_sharpen_frag, work));
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kAmount) < 0.01f; }
    f32 input_margin(const EffectEval&) const noexcept override { return 1.0f; }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        struct {
            Vec4 uvMap;
            Vec4 texel;
        } u{};
        // Passo do núcleo: 1 pixel da layer, mas nunca menos que 1 texel. No
        // preview reduzido o detalhe de um pixel da layer não existe na
        // textura; afiar abaixo do texel não teria efeito visível.
        const f32 stepX = std::max(1.0f, input.texel_scale_x());
        const f32 stepY = std::max(1.0f, input.texel_scale_y());
        u.uvMap = Vec4{1.0f, 1.0f, 0.0f, 0.0f};
        u.texel = Vec4{stepX / static_cast<f32>(input.width), stepY / static_cast<f32>(input.height),
                       reference_ ? std::clamp(std::floor(e.f(kAmount)), 0.0f, 4000.0f) : e.f(kAmount) / 100.0f,
                       reference_ ? 1.0f : 0.0f};
        out = input;
        out.texture = ctx.texture("nitidez", input.width, input.height);
        if (ctx.fullscreen_pass("nitidez", PassStage::Effects, out.texture, ShaderId::effects_sharpen_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
private:
    bool reference_;
};

// -----------------------------------------------------------------------------
// Desfoque de zoom — rastro radial: cada pixel é a média de amostras tiradas
// ao longo da reta que liga o pixel ao centro. É o "empurrar a lente": as
// bordas correm mais que o centro, porque o passo é proporcional à distância
// até ele.
// -----------------------------------------------------------------------------
class ZoomBlur final : public Effect {
public:
    // Novo no FIM (0.4.5, o "Zoom" do CC Radial Fast Blur): Normal é a média
    // de sempre; Mais claro/Mais escuro guardam a amostra mais clara/escura do
    // rastro (as luzes ou as sombras se espalham).
    enum : u32 { kCenter = 0, kAmount, kRepeat, kMix, kMode, kAdaptive };
    static constexpr u32 kSeparation = kMode;

    explicit ZoomBlur(bool chromatic = false) : chromatic_(chromatic) {}

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kZoomBlur, "Desfoque de zoom", "Desfoque",
                                  EffectClass::Domain};
        static const EffectInfo chromatic{effect_keys::kChromaticZoomBlur, "Desfoque de zoom cromático",
                                          "Desfoque", EffectClass::Domain};
        return chromatic_ ? chromatic : i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        // O rastro é uma fração da distância ao centro: 1.0 atravessa o quadro
        // inteiro até o centro. O sinal troca o sentido.
        p.add_float("amount", "Intensidade", 20.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(-400.0f, 400.0f);
        p.add_bool("repeat_edge", "Repetir a borda", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        if (chromatic_) {
            p.add_float("separation", "Separação RGB", 30.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
            p.add_bool("adaptive", "Amostragem adaptativa", true);
        } else {
            static const char* const kModes[] = {"Normal", "Mais claro", "Mais escuro"};
            p.add_enum("mode", "Modo", kModes, 3, 0);
        }
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(e.f(kAmount)) < 0.01f || e.f(kMix) < 0.01f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmount] = ParamValue::scalar(35.0f);
        return true;
    }
    // Sem margem: o rastro é medido dentro da própria imagem (a amostra anda
    // ENTRE o pixel e o centro, nunca além deles), e a região ficar do mesmo
    // tamanho mantém a conta em uv — sem conversão de espaço de região.
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_zoom_blur_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        Vec2 c = e.p2(kCenter);
        if (chromatic_ && e.placement) {
            c.x = (c.x * e.placement->layerWidth - input.region.x) / std::max(input.region.w, 1.f);
            c.y = (c.y * e.placement->layerHeight - input.region.y) / std::max(input.region.h, 1.f);
        }
        u.p0 = Vec4{c.x, c.y, std::clamp(e.f(kAmount) * 0.01f, -4.0f, 4.0f) * 0.5f,
                    e.b(kRepeat) ? 1.0f : 0.0f};
        // A mistura com o original: o rastro nunca é o único resultado.
        const f32 k = std::clamp(e.f(kMix) * 0.01f, 0.0f, 1.0f);
        u.p1 = chromatic_
            ? Vec4{k, 0.f, std::clamp(e.f(kSeparation) * .01f, 0.f, 1.f), e.b(kAdaptive) ? 1.f : 0.f}
            : Vec4{k, e.count > kMode ? static_cast<f32>(std::min(e.e(kMode), 2u)) : 0.f, 0.f, 0.f};
        const CommonSampler sampler = e.b(kRepeat) ? CommonSampler::LinearRepeat : CommonSampler::LinearClamp;
        out = input;
        out.texture = ctx.texture("desfoque-zoom", input.width, input.height);
        if (ctx.fullscreen_pass("desfoque-zoom", PassStage::Effects, out.texture,
                                ShaderId::effects_zoom_blur_frag,
                                {PassTexture{input.texture, {}, sampler}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
private:
    bool chromatic_;
};

enum class PhotographicBlurMode : u32 {
    Encode, Color, Channels, Chroma, Focus, Directional, Motion, Edge, Sharpen, Prism, PrismFinish
};

struct PhotographicBlurUniforms {
    Vec4 flags; // mode, gain, offset, original mix
    Vec4 p0;
    Vec4 p1;
    Vec4 p2;
    Vec4 p3;
    Vec4 p4;
};

constexpr ShaderId kPhotographicBlurShader = ShaderId::effects_photographic_blur_frag;
constexpr u32 kPhotographicFraction = kParamAnimatable | kParamPercent;
constexpr const char* kBlurKernels[] = {"Gaussiano", "Caixa", "Triangular"};
constexpr const char* kBlurBorders[] = {"Refletir", "Repetir pixels da borda", "Transparente", "Repetir imagem"};
constexpr CommonSampler kBlurSamplers[] = {
    CommonSampler::LinearMirror, CommonSampler::LinearClamp, CommonSampler::LinearBorder, CommonSampler::LinearRepeat,
};

f32 blur_setting(f32 value, f32 fallback, f32 lo, f32 hi) noexcept {
    return std::clamp(std::isfinite(value) ? value : fallback, lo, hi);
}

Status photographic_pass(EffectBuildContext& ctx, const LayerImage& input,
                          const PhotographicBlurUniforms& u, const PassTexture* textures, u32 count,
                          const char* name, LayerImage& out) {
    out = input;
    out.texture = ctx.texture(name, input.width, input.height);
    if (ctx.fullscreen_pass(name, PassStage::Effects, out.texture, kPhotographicBlurShader,
                            textures, count, &u, sizeof(u)) == kInvalidIndex) {
        return Errc::PipelineCompileFailed;
    }
    return OkStatus;
}

Status pack_blur_source(EffectBuildContext& ctx, const LayerImage& input, LayerImage& out) {
    const PassTexture source{input.texture};
    return photographic_pass(ctx, input, {}, &source, 1, "blur-fotografico-codificar", out);
}

class PhotographicBlur final : public Effect {
public:
    explicit PhotographicBlur(PhotographicBlurMode mode) : mode_(mode) {}

    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {"aurea.blur.photographic", "Desfoque fotográfico", "Desfoque", EffectClass::Neighborhood},
            {"aurea.blur.rgb_channels", "Desfoque por canais RGB", "Desfoque", EffectClass::Neighborhood},
            {"aurea.blur.chroma_luma", "Desfoque de croma e luma", "Desfoque", EffectClass::Neighborhood},
            {"aurea.blur.soft_focus", "Foco suave", "Desfoque", EffectClass::Neighborhood},
            {"aurea.blur.edge_masked", "Desfoque de contornos", "Desfoque", EffectClass::Neighborhood},
            {"aurea.blur.detail_sharpen", "Nitidez de detalhes", "Nitidez", EffectClass::Neighborhood},
        };
        return infos[mode_ <= PhotographicBlurMode::Focus ? static_cast<u32>(mode_) - 1
                                                          : static_cast<u32>(mode_) - 3];
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", amount_label(), default_amount(), 0, 400, kPhotographicFraction, "%");
        p.add_float("width_x", "Largura relativa X", 1, 0, 8, kParamAnimatable);
        p.add_float("width_y", "Largura relativa Y", 1, 0, 8, kParamAnimatable);
        p.add_enum("kernel", "Forma do desfoque", kBlurKernels, 3, 0);
        p.add_enum("boundary", "Bordas", kBlurBorders, 4, 0);
        p.add_float("gain", "Brilho", 1, 0, 8, kParamAnimatable);
        p.add_float("offset", "Deslocamento dos escuros", 0, -8, 2, kParamAnimatable);
        p.add_float("mix_source", "Mistura com o original", 0, 0, 100, kPhotographicFraction, "%");
        switch (mode_) {
            case PhotographicBlurMode::Color:
                p.add_float("opacity_scale", "Escala da opacidade", 1, 0, 8, kParamAnimatable);
                break;
            case PhotographicBlurMode::Channels:
                p.add_float("blur_red", "Desfoque do vermelho", 0, 0, 8, kParamAnimatable);
                p.add_float("blur_green", "Desfoque do verde", 0.5f, 0, 8, kParamAnimatable);
                p.add_float("blur_blue", "Desfoque do azul", 1, 0, 8, kParamAnimatable);
                p.add_float("blur_alpha", "Desfoque do alfa", 0, 0, 8, kParamAnimatable);
                break;
            case PhotographicBlurMode::Chroma:
                p.add_float("luma", "Desfoque da luma", 0, 0, 400, kPhotographicFraction, "%");
                p.add_float("chroma_scale", "Escala da croma", 1, 0, 8, kParamAnimatable);
                p.add_float("luma_scale", "Escala da luma", 1, 0, 8, kParamAnimatable);
                break;
            case PhotographicBlurMode::Focus:
                p.add_float("mix_blur", "Mistura com o desfoque", 0, 0, 100, kPhotographicFraction, "%");
                break;
            case PhotographicBlurMode::Edge:
                p.add_float("edge_width", "Largura do contorno", 11.2f, 0, 400, kPhotographicFraction, "%");
                p.add_float("edge_strength", "Força do contorno", 0.5f, 0, 8, kParamAnimatable);
                p.add_float("edge_threshold", "Limiar do contorno", 0, 0, 1, kParamAnimatable);
                break;
            case PhotographicBlurMode::Sharpen:
                p.add_float("strength", "Intensidade", 1, -3, 8, kParamAnimatable);
                p.add_float("detail_size", "Tamanho do detalhe", 1, 0.05f, 16, kParamAnimatable | kParamPixels, "px");
                p.add_float("chroma", "Nitidez da croma", 1, 0, 4, kParamAnimatable);
                break;
            default: break;
        }
    }

    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMixSource) >= 100.0f; }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(kPhotographicBlurShader, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_downsample_frag, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        LayerImage encoded;
        if (auto status = pack_blur_source(ctx, input, encoded); !status.ok()) return status;
        const auto setting = [&](u32 index, f32 fallback, f32 lo, f32 hi) {
            return blur_setting(e.f(index), fallback, lo, hi);
        };
        const f32 amount = setting(kAmount, default_amount(), 0, 400);
        const f32 scaleX = setting(kAxisX, 1, 0, 8);
        const f32 scaleY = setting(kAxisY, 1, 0, 8);
        const auto sampler = kBlurSamplers[std::min<u32>(e.e(kBoundary), 3)];
        const auto kernel = static_cast<BlurKernel>(std::min<u32>(e.e(kKernel), 2));
        const auto blur = [&](f32 sigma, LayerImage& target) {
            BlurRequest request;
            request.sigmaX = std::min(sigma * scaleX, 3000.0f);
            request.sigmaY = std::min(sigma * scaleY, 3000.0f);
            return build_paired_blur(ctx, encoded, request, sampler, kernel, target);
        };
        PhotographicBlurUniforms u{};
        u.flags = Vec4{static_cast<f32>(mode_), setting(kGain, 1, 0, 8), setting(kOffset, 0, -8, 2),
                       setting(kMixSource, 0, 0, 100) * 0.01f};
        LayerImage result[4]{encoded, encoded, encoded, encoded};
        u32 count = 1;
        if (mode_ == PhotographicBlurMode::Channels) {
            count = 4;
            const f32 defaults[]{0.0f, 0.5f, 1.0f, 0.0f};
            for (u32 channel = 0; channel < count; ++channel) {
                const f32 sigma = amount * setting(kExtra + channel, defaults[channel], 0, 8) * 1.3305f;
                if (auto status = blur(sigma, result[channel]); !status.ok()) return status;
            }
        } else {
            const f32 sigma = mode_ == PhotographicBlurMode::Sharpen ? setting(kExtra + 1, 1, 0.05f, 16) * 0.86f
                                                                   : amount * 0.831f;
            if (auto status = blur(sigma, result[0]); !status.ok()) return status;
            switch (mode_) {
                case PhotographicBlurMode::Color:
                    u.p0.x = setting(kExtra, 1, 0, 8);
                    break;
                case PhotographicBlurMode::Chroma:
                    count = 2;
                    if (auto status = blur(setting(kExtra, 0, 0, 400) * 0.831f, result[1]); !status.ok()) return status;
                    u.p0 = Vec4{setting(kExtra + 1, 1, 0, 8), setting(kExtra + 2, 1, 0, 8), 0, 0};
                    break;
                case PhotographicBlurMode::Focus:
                    u.p0.x = 0.5f + 0.005f * setting(kExtra, 0, 0, 100);
                    break;
                case PhotographicBlurMode::Edge:
                    count = 2;
                    if (auto status = blur(setting(kExtra, 11.2f, 0, 400) * 0.831f, result[1]);
                        !status.ok()) return status;
                    u.p0 = Vec4{setting(kExtra + 1, 0.5f, 0, 8), setting(kExtra + 2, 0, 0, 1), 0, 0};
                    break;
                case PhotographicBlurMode::Sharpen:
                    count = 2;
                    if (auto status = blur(amount * 0.831f, result[1]); !status.ok()) return status;
                    u.p0 = Vec4{setting(kExtra, 1, -3, 8), setting(kExtra + 2, 1, 0, 4), 0, 0};
                    break;
                default: break;
            }
        }
        PassTexture textures[5]{{encoded.texture}};
        for (u32 channel = 0; channel < count; ++channel) textures[channel + 1] = PassTexture{result[channel].texture};
        return photographic_pass(ctx, input, u, textures, count + 1, "blur-fotografico-final", out);
    }

private:
    enum : u32 { kAmount, kAxisX, kAxisY, kKernel, kBoundary, kGain, kOffset, kMixSource, kExtra };
    PhotographicBlurMode mode_;

    f32 default_amount() const noexcept {
        if (mode_ == PhotographicBlurMode::Focus) return 22.4f;
        if (mode_ == PhotographicBlurMode::Edge || mode_ == PhotographicBlurMode::Sharpen) return 11.2f;
        return 40.0f;
    }
    const char* amount_label() const noexcept {
        if (mode_ == PhotographicBlurMode::Chroma) return "Desfoque da croma";
        if (mode_ == PhotographicBlurMode::Sharpen) return "Largura da nitidez";
        return "Desfoque";
    }
};

class PhotographicPathBlur final : public Effect {
public:
    explicit PhotographicPathBlur(bool motion) : motion_(motion) {}

    const EffectInfo& info() const noexcept override {
        static const EffectInfo direction{"aurea.blur.photographic_direction", "Desfoque direcional fotográfico",
                                          "Desfoque", EffectClass::Neighborhood};
        static const EffectInfo motion{"aurea.blur.transform_motion", "Desfoque entre transformações",
                                       "Desfoque", EffectClass::Domain};
        return motion_ ? motion : direction;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("gain", "Brilho", 1, 0, 8, kParamAnimatable);
        p.add_float("offset", "Deslocamento dos escuros", 0, -8, 2, kParamAnimatable);
        p.add_float("mix_source", "Mistura com o original", 0, 0, 100, kPhotographicFraction, "%");
        p.add_enum("boundary", "Bordas", kBlurBorders, 4, motion_ ? 2 : 0);
        p.add_float("bias", "Viés da exposição", 50, 0, 100, kPhotographicFraction, "%");
        if (motion_) {
            p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
            p.add_float("from_distance", "Distância inicial", 1, 0.001f, 8, kParamAnimatable);
            p.add_float("to_distance", "Distância final", 0.8f, 0.001f, 8, kParamAnimatable);
            p.add_angle("from_rotate", "Rotação inicial", 0);
            p.add_angle("to_rotate", "Rotação final", 0);
            p.add_point2("from_shift", "Deslocamento inicial", Vec2{}, -2.0f, 2.0f, kParamAnimatable | kParamRelative);
            p.add_point2("to_shift", "Deslocamento final", Vec2{}, -2.0f, 2.0f, kParamAnimatable | kParamRelative);
        } else {
            p.add_float("amount", "Largura do desfoque", 96, 0, 2048, kParamAnimatable | kParamPixels, "px");
            p.add_angle("angle", "Ângulo", 45);
            p.add_float("shift", "Deslocamento", 0, -4, 4, kParamAnimatable);
            p.add_enum("kernel", "Forma do desfoque", kBlurKernels, 3, 1);
        }
    }

    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMixSource) >= 100.0f; }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(kPhotographicBlurShader, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        LayerImage encoded;
        if (auto status = pack_blur_source(ctx, input, encoded); !status.ok()) return status;
        const auto setting = [&](u32 index, f32 fallback, f32 lo, f32 hi) {
            return blur_setting(e.f(index), fallback, lo, hi);
        };
        const auto finite_point = [&](u32 index, Vec2 fallback) {
            const Vec2 p = e.p2(index);
            return Vec2{blur_setting(p.x, fallback.x, -2, 2), blur_setting(p.y, fallback.y, -2, 2)};
        };
        const auto radians = [&](u32 index, f32 fallback) {
            return setting(index, fallback, -360000.0f, 360000.0f) * kPi / 180.0f;
        };
        const auto mode = motion_ ? PhotographicBlurMode::Motion : PhotographicBlurMode::Directional;
        PhotographicBlurUniforms u{};
        u.flags = Vec4{static_cast<f32>(mode), setting(kGain, 1, 0, 8), setting(kOffset, 0, -8, 2),
                       setting(kMixSource, 0, 0, 100) * 0.01f};
        const f32 bias = setting(kBias, 50, 0, 100) * 0.01f;
        if (motion_) {
            Vec2 center = finite_point(kPath, {0.5f, 0.5f});
            if (e.placement && e.placement->layerWidth > 0 && e.placement->layerHeight > 0) {
                center.x = (center.x * e.placement->layerWidth - input.region.x) / std::max(input.region.w, 1.0f);
                center.y = (center.y * e.placement->layerHeight - input.region.y) / std::max(input.region.h, 1.0f);
            }
            const Vec2 from = finite_point(kPath + 5, {});
            const Vec2 to = finite_point(kPath + 6, {});
            u.p0 = Vec4{center.x, center.y, input.region.w / std::max(input.region.h, 1.0f), bias};
            u.p1 = Vec4{setting(kPath + 1, 1, 0.001f, 8), setting(kPath + 2, 0.8f, 0.001f, 8),
                        radians(kPath + 3, 0), radians(kPath + 4, 0)};
            u.p2 = Vec4{from.x, from.y, to.x, to.y};
        } else {
            const f32 width = setting(kPath, 96, 0, 2048);
            const f32 angle = radians(kPath + 1, 45);
            u.p0 = Vec4{std::cos(angle) * width / std::max(input.region.w, 1.0f),
                        -std::sin(angle) * width / std::max(input.region.h, 1.0f),
                        setting(kPath + 2, 0, -4, 4), bias};
            u.p1.x = static_cast<f32>(std::min<u32>(e.e(kPath + 3), 2));
        }
        const PassTexture source{encoded.texture, {}, kBlurSamplers[std::min<u32>(e.e(kBoundary), 3)]};
        return photographic_pass(ctx, input, u, &source, 1, "desfoque-trajetoria", out);
    }

private:
    enum : u32 { kGain, kOffset, kMixSource, kBoundary, kBias, kPath };
    bool motion_;
};

class MotionCurveBlur final : public Effect {
public:
    enum : u32 { kDistance, kRotation, kShift, kCenter, kDuration, kShutterShift, kBias, kBoundary, kMode };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.blur.motion_curves", "Desfoque de curvas de movimento", "Desfoque", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Transformar e desfocar", "Apenas desfoque"};
        p.add_float("distance", "Distância", 1, .001f, 8);
        p.add_angle("rotation", "Rotação", 0);
        p.add_point2("shift", "Deslocamento", Vec2{}, -2, 2, kParamAnimatable | kParamRelative);
        p.add_point2("center", "Centro", Vec2{.5f, .5f}, -2, 2, kParamAnimatable | kParamRelative);
        p.add_float("duration", "Duração do obturador", 1, 0, 8, kParamAnimatable | kParamFrames);
        p.add_float("shutter_shift", "Deslocamento do obturador", 0, -4, 4, kParamAnimatable | kParamFrames);
        p.add_float("bias", "Viés da exposição", 50, 0, 100, kPhotographicFraction, "%");
        p.add_enum("boundary", "Bordas", kBlurBorders, 4, 2);
        p.add_enum("mode", "Modo", modes, 2, 0);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kDistance] = ParamValue::scalar(.8f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(kPhotographicBlurShader, work));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        if (!e.layer || !e.instance) return;
        static const ParameterRegistry specs = [] {
            ParameterRegistry params;
            MotionCurveBlur{}.declare_parameters(params);
            return params;
        }();
        const f64 width = blur_setting(e.f(kDuration), 1, 0, 8);
        const f64 mid = e.time_frames() + (width > 0 ? blur_setting(e.f(kShutterShift), 0, -4, 4) : 0);
        auto sample = [&](f64 t) {
            auto value = [&](u32 slot) {
                if (e.instance->params[slot].source == ParamSource::Expression) return e.value(slot);
                return evaluate_param_f(e.layer->tracks, *e.instance, slot, specs.at(slot), t);
            };
            const Vec2 shift = value(kShift).as_vec2();
            return Vec4{value(kDistance).v[0], value(kRotation).v[0], shift.x, shift.y};
        };
        e.auxMapX = sample(mid - width * .5);
        e.auxMapY = sample(mid + width * .5);
        e.auxInfo.x = 1;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 currentShift = e.p2(kShift);
        const Vec4 current{e.f(kDistance), e.f(kRotation), currentShift.x, currentShift.y};
        auto sanitize = [](Vec4 v) {
            return Vec4{blur_setting(v.x, 1, .001f, 8), -blur_setting(v.y, 0, -36000, 36000) * kDeg2Rad,
                        blur_setting(v.z, 0, -2, 2), blur_setting(v.w, 0, -2, 2)};
        };
        const Vec4 now = sanitize(current);
        Vec4 from = e.auxInfo.x > .5f ? sanitize(e.auxMapX) : now;
        Vec4 to = e.auxInfo.x > .5f ? sanitize(e.auxMapY) : now;
        const f32 w = std::max(input.region.w, 1.0f), h = std::max(input.region.h, 1.0f);
        const f32 layerWidth = e.placement && e.placement->layerWidth ? f32(e.placement->layerWidth) : w;
        const f32 layerHeight = e.placement && e.placement->layerHeight ? f32(e.placement->layerHeight) : h;
        const Vec2 center = e.p2(kCenter);
        const f32 cx = (blur_setting(center.x, .5f, -2, 2) * layerWidth - input.region.x) / w;
        const f32 cy = (blur_setting(center.y, .5f, -2, 2) * layerHeight - input.region.y) / h;
        auto place = [&](Vec4& endpoint) {
            if (e.e(kMode) == 1) {
                const f32 x = (endpoint.z - now.z) * layerWidth;
                const f32 y = (endpoint.w - now.w) * layerHeight;
                const f32 c = std::cos(now.y), s = std::sin(now.y);
                endpoint = {endpoint.x / now.x, endpoint.y - now.y,
                            (c * x - s * y) * now.x / w, (s * x + c * y) * now.x / h};
            } else {
                endpoint.z *= layerWidth / w;
                endpoint.w *= layerHeight / h;
            }
        };
        place(from);
        place(to);
        auto unchanged = [](Vec4 v) {
            return v.x == 1 && v.y == 0 && v.z == 0 && v.w == 0;
        };
        if (unchanged(from) && unchanged(to)) { out = input; return OkStatus; }

        PhotographicBlurUniforms u{};
        // Mode 13 integrates directly in linear light, preserving HDR and alpha.
        u.flags.x = 13;
        u.p0 = {cx, cy, w / h, blur_setting(e.f(kBias), 50, 0, 100) * .01f};
        u.p1 = {from.x, to.x, from.y, to.y};
        u.p2 = {from.z, from.w, to.z, to.w};
        const bool staticTransform = from.x == to.x && from.y == to.y && from.z == to.z && from.w == to.w;
        u.p3.x = staticTransform ? 1.0f : 32.0f;
        const PassTexture source{input.texture, {}, kBlurSamplers[std::min<u32>(e.e(kBoundary), 3)]};
        return photographic_pass(ctx, input, u, &source, 1, "desfoque-curvas-movimento", out);
    }
};

class ExposureStreaks final : public Effect {
public:
    enum : u32 { Center, FromDistance, ToDistance, FromAngle, ToAngle, FromShift, ToShift,
                 Bias, Gain, Threshold, ThresholdColor, Darks, Brights, Exposure, Alpha, Combine, Boundary };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.light.exposure_streaks", "Trilhas de exposição", "Luz",
                                  EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        constexpr auto relative = kParamAnimatable | kParamRelative;
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.f, 2.f, relative);
        p.add_float("from_distance", "Distância inicial", 1.f, 0.001f, 8.f, kParamAnimatable);
        p.add_float("to_distance", "Distância final", 0.8f, 0.001f, 8.f, kParamAnimatable);
        p.add_angle("from_angle", "Rotação inicial", 0.f);
        p.add_angle("to_angle", "Rotação final", 0.f);
        p.add_point2("from_shift", "Deslocamento inicial", Vec2{}, -2.f, 2.f, relative);
        p.add_point2("to_shift", "Deslocamento final", Vec2{}, -2.f, 2.f, relative);
        p.add_float("bias", "Viés da exposição", 0.f, 0.f, 100.f, kPhotographicFraction, "%");
        p.add_float("gain", "Brilho das trilhas", 1.f, 0.f, 8.f, kParamAnimatable);
        p.add_float("threshold", "Limiar", 50.f, 0.f, 100.f, kPhotographicFraction, "%");
        p.add_color("threshold_color", "Cor do limiar", Vec4{0, 0, 0, 1});
        p.add_float("mix_darks", "Mistura das sombras", 100.f, 0.f, 100.f, kPhotographicFraction, "%");
        p.add_float("mix_brights", "Mistura das luzes", 0.f, 0.f, 100.f, kPhotographicFraction, "%");
        p.add_float("exposure", "Brilho", 1.f, 0.f, 8.f, kParamAnimatable);
        p.add_float("alpha", "Influência no alfa", 1.f, 0.f, 4.f, kParamAnimatable);
        static constexpr const char* operations[] = {"Somar", "Tela"};
        p.add_enum("combine", "Combinação", operations, 2, 0);
        p.add_enum("boundary", "Bordas", kBlurBorders, 4, 2);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(kPhotographicBlurShader, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        LayerImage encoded;
        if (auto status = pack_blur_source(ctx, input, encoded); !status.ok()) return status;
        const auto value = [&](u32 slot, f32 fallback, f32 lo, f32 hi) {
            return blur_setting(e.f(slot), fallback, lo, hi);
        };
        const auto percent = [&](u32 slot, f32 fallback) { return value(slot, fallback, 0.f, 100.f) * 0.01f; };
        const auto point = [&](u32 slot, f32 fallback) {
            const Vec2 v = e.p2(slot);
            return Vec2{blur_setting(v.x, fallback, -2.f, 2.f), blur_setting(v.y, fallback, -2.f, 2.f)};
        };
        PhotographicBlurUniforms u{};
        u.flags.x = 11.f;
        u.p3.x = percent(Threshold, 50.f);
        u.p4 = e.color(ThresholdColor);
        LayerImage highlights;
        const PassTexture packed{encoded.texture, {}, CommonSampler::LinearClamp};
        if (auto status = photographic_pass(ctx, input, u, &packed, 1, "trilhas-separar-luzes", highlights);
            !status.ok()) return status;

        const f32 rw = std::max(input.region.w, 1.f);
        const f32 rh = std::max(input.region.h, 1.f);
        Vec2 center = point(Center, 0.5f);
        Vec2 fromShift = point(FromShift, 0.f);
        Vec2 toShift = point(ToShift, 0.f);
        if (e.placement && e.placement->layerWidth > 0 && e.placement->layerHeight > 0) {
            const f32 w = static_cast<f32>(e.placement->layerWidth);
            const f32 h = static_cast<f32>(e.placement->layerHeight);
            center = Vec2{(center.x * w - input.region.x) / rw, (center.y * h - input.region.y) / rh};
            fromShift = Vec2{fromShift.x * w / rw, fromShift.y * h / rh};
            toShift = Vec2{toShift.x * w / rw, toShift.y * h / rh};
        }
        const f32 from = value(FromDistance, 1.f, 0.001f, 8.f);
        const f32 to = value(ToDistance, 0.8f, 0.001f, 8.f);
        const f32 fromAngle = value(FromAngle, 0.f, -36000.f, 36000.f) * kPi / 180.f;
        const f32 toAngle = value(ToAngle, 0.f, -36000.f, 36000.f) * kPi / 180.f;
        const f32 extent = std::max(static_cast<f32>(input.width), static_cast<f32>(input.height));
        const f32 travel = (std::fabs(to - from) * 0.5f + std::fabs(toAngle - fromAngle) * 0.5f +
                            std::max(std::fabs(toShift.x - fromShift.x), std::fabs(toShift.y - fromShift.y))) * extent;
        u.flags = Vec4{12.f, value(Gain, 1.f, 0.f, 8.f), value(Exposure, 1.f, 0.f, 8.f),
                       value(Alpha, 1.f, 0.f, 4.f)};
        u.p0 = Vec4{center.x, center.y, rw / rh, percent(Bias, 0.f)};
        u.p1 = Vec4{from, to, fromAngle, toAngle};
        u.p2 = Vec4{fromShift.x, fromShift.y, toShift.x, toShift.y};
        u.p3 = Vec4{percent(Darks, 100.f), percent(Brights, 0.f), e.e(Combine) == 1 ? 1.f : 0.f,
                    std::clamp(std::ceil(travel), 16.f, 128.f)};
        const PassTexture textures[] = {packed,
            {highlights.texture, {}, kBlurSamplers[std::min<u32>(e.e(Boundary), 3)]}};
        return photographic_pass(ctx, input, u, textures, 2, "trilhas-integrar-exposicao", out);
    }
};

class PrismDefocus final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.blur.prism_defocus", "Desfoque prismático", "Desfoque",
                                  EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const shapes[] = {"Círculo", "Triângulo", "Quadrado", "Pentágono", "Hexágono", "Octógono"};
        p.add_float("width", "Largura do desfoque", 21.12f, 0, 1024, kParamAnimatable | kParamPixels, "px");
        p.add_float("height", "Altura relativa", 1, 0.01f, 8, kParamAnimatable);
        p.add_enum("shape", "Forma da abertura", shapes, 6, 0);
        p.add_angle("rotation", "Rotação da abertura", 0);
        p.add_float("separation", "Separação cromática", 0.3f, -1, 1, kParamAnimatable);
        p.add_float("ring_thickness", "Espessura dos anéis", 0.3f, 0.01f, 2, kParamAnimatable);
        p.add_float("chromatic_warp", "Distorção cromática", 0, -2, 2, kParamAnimatable);
        p.add_float("gamma", "Gama", 1, 0.1f, 10, kParamAnimatable);
        p.add_float("gain", "Brilho", 1, 0, 8, kParamAnimatable);
        p.add_float("offset", "Deslocamento dos escuros", 0, -8, 2, kParamAnimatable);
        p.add_float("mix_source", "Mistura com o original", 0, 0, 100, kPhotographicFraction, "%");
        p.add_enum("boundary", "Bordas", kBlurBorders, 4, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMixSource) >= 100.0f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(kPhotographicBlurShader, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_downsample_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        LayerImage encoded;
        if (auto status = pack_blur_source(ctx, input, encoded); !status.ok()) return status;
        const auto setting = [&](u32 index, f32 fallback, f32 lo, f32 hi) {
            return blur_setting(e.f(index), fallback, lo, hi);
        };
        const f32 width = setting(kWidth, 21.12f, 0, 1024);
        const f32 height = setting(kHeight, 1, 0.01f, 8);
        const auto sampler = kBlurSamplers[std::min<u32>(e.e(kBoundary), 3)];
        LayerImage sample = encoded;
        if (width * std::min(input.texel_scale_x(), input.texel_scale_y() * height) > 12 &&
            sample.width > 1 && sample.height > 1) {
            sample.width = (sample.width + 1) / 2;
            sample.height = (sample.height + 1) / 2;
            sample.texture = ctx.texture("prisma-reduzir", sample.width, sample.height);
            DownsampleUniforms down{Vec4{1, 1, 0, 0}, Vec4{1.0f / input.width, 1.0f / input.height, 0, 0}};
            if (ctx.fullscreen_pass("prisma-reduzir", PassStage::Effects, sample.texture, ShaderId::effects_downsample_frag,
                                    {PassTexture{encoded.texture, {}, sampler}}, &down, sizeof(down)) == kInvalidIndex) {
                return Errc::PipelineCompileFailed;
            }
        }
        const u32 sides[]{0, 3, 4, 5, 6, 8};
        PhotographicBlurUniforms u{};
        u.flags.x = static_cast<f32>(PhotographicBlurMode::Prism);
        u.p0 = Vec4{width / std::max(input.region.w, 1.0f), width * height / std::max(input.region.h, 1.0f),
                    setting(kSeparation, 0.3f, -1, 1), setting(kThickness, 0.3f, 0.01f, 2)};
        u.p1 = Vec4{setting(kRotate, 0, -360000, 360000) * kDeg2Rad,
                    static_cast<f32>(sides[std::min<u32>(e.e(kShape), 5)]),
                    setting(kWarp, 0, -2, 2), setting(kGamma, 1, 0.1f, 10)};
        const PassTexture texture{sample.texture, {}, sampler};
        LayerImage rings;
        if (auto status = photographic_pass(ctx, sample, u, &texture, 1, "prisma-aneis-rgb", rings); !status.ok()) {
            return status;
        }
        u.flags = Vec4{static_cast<f32>(PhotographicBlurMode::PrismFinish), setting(kGain, 1, 0, 8),
                       setting(kOffset, 0, -8, 2), setting(kMixSource, 0, 0, 100) * 0.01f};
        const PassTexture result[]{PassTexture{encoded.texture}, PassTexture{rings.texture}};
        return photographic_pass(ctx, input, u, result, 2, "prisma-compor", out);
    }
private:
    enum : u32 { kWidth, kHeight, kShape, kRotate, kSeparation, kThickness, kWarp, kGamma,
                 kGain, kOffset, kMixSource, kBoundary };
};

} // namespace

void register_blur_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<ExposureStreaks>());
    (void)r.add(std::make_unique<GaussianBlur>());
    (void)r.add(std::make_unique<Sharpen>());
    (void)r.add(std::make_unique<Sharpen>(true));
    (void)r.add(std::make_unique<ZoomBlur>());
    (void)r.add(std::make_unique<ZoomBlur>(true));
    for (const auto mode : {PhotographicBlurMode::Color, PhotographicBlurMode::Channels,
                            PhotographicBlurMode::Chroma, PhotographicBlurMode::Focus, PhotographicBlurMode::Edge,
                            PhotographicBlurMode::Sharpen}) {
        (void)r.add(std::make_unique<PhotographicBlur>(mode));
    }
    (void)r.add(std::make_unique<PhotographicPathBlur>(false));
    (void)r.add(std::make_unique<PhotographicPathBlur>(true));
    (void)r.add(std::make_unique<MotionCurveBlur>());
    (void)r.add(std::make_unique<PrismDefocus>());
}

} // namespace aurea::builtin
