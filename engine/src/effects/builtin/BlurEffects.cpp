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
void gaussian_pairs(f32 sigma, BlurUniforms& u) noexcept {
    const u32 radius = std::min<u32>(kMaxPairs * 2, std::max<u32>(1, static_cast<u32>(std::ceil(3.0f * sigma))));
    f32 w[kMaxPairs * 2 + 2]{};
    const f32 inv2s2 = 1.0f / (2.0f * sigma * sigma);
    f32 sum = 0.0f;
    for (u32 i = 0; i <= radius; ++i) {
        w[i] = std::exp(-static_cast<f32>(i * i) * inv2s2);
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

Status build_gaussian(EffectBuildContext& ctx, const LayerImage& input, const BlurRequest& req,
                      LayerImage& out) {
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
    const CommonSampler sampler = req.repeatEdges ? CommonSampler::LinearClamp : CommonSampler::LinearBorder;

    u32 w = 0, h = 0;
    ctx.region_size(region, kx, w, h);

    FGTexture src = input.texture;
    Rect srcRegion = input.region;
    u32 srcW = input.width, srcH = input.height;

    for (u32 level = 0; level < kMaxReductions; ++level) {
        const bool redX = sx > kMaxSigmaTexels && w > 1;
        const bool redY = sy > kMaxSigmaTexels && h > 1;
        if (!redX && !redY) break;
        const u32 nw = redX ? std::max(1u, (w + 1) / 2) : w;
        const u32 nh = redY ? std::max(1u, (h + 1) / 2) : h;
        const FGTexture dst = ctx.texture(nameReduce, nw, nh);
        DownsampleUniforms u{};
        u.uvMap = EffectBuildContext::uv_map(region, srcRegion);
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
        src = dst; srcRegion = region; srcW = nw; srcH = nh;
        w = nw; h = nh;
    }

    auto separable = [&](bool horizontal, f32 sigma) -> bool {
        const FGTexture dst = ctx.texture(horizontal ? nameH : nameV, w, h);
        BlurUniforms u{};
        u.uvMap = EffectBuildContext::uv_map(region, srcRegion);
        u.dir = horizontal ? Vec4{1.0f / static_cast<f32>(srcW), 0.0f, 0.0f, 0.0f}
                           : Vec4{0.0f, 1.0f / static_cast<f32>(srcH), 0.0f, 0.0f};
        gaussian_pairs(sigma, u);
        // O bloco vai inteiro (240 bytes): o intervalo amarrado precisa cobrir
        // o bloco declarado no shader, e o laço lê só os pares válidos.
        if (ctx.fullscreen_pass(horizontal ? nameH : nameV, PassStage::Effects, dst,
                                ShaderId::effects_gaussian_blur_frag,
                                {PassTexture{src, {}, sampler}}, &u, sizeof(u)) == kInvalidIndex) {
            return false;
        }
        src = dst; srcRegion = region; srcW = w; srcH = h;
        return true;
    };

    if (sx >= 0.05f && !separable(true, sx)) return Errc::PipelineCompileFailed;
    if (sy >= 0.05f && !separable(false, sy)) return Errc::PipelineCompileFailed;

    out.texture = src;
    out.region = region;
    out.width = srcW;
    out.height = srcH;
    return OkStatus;
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
        req.label = "blur";
        return build_gaussian(ctx, input, req, out);
    }
};

// -----------------------------------------------------------------------------
// Sharpen
// -----------------------------------------------------------------------------
class Sharpen final : public Effect {
public:
    enum : u32 { kAmount = 0 };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSharpen, "Nitidez", "Desfoque", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", "Intensidade", 0.0f, 0.0f, 500.0f, kParamAnimatable | kParamPercent, "%");
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
                       e.f(kAmount) / 100.0f, 0.0f};
        out = input;
        out.texture = ctx.texture("nitidez", input.width, input.height);
        if (ctx.fullscreen_pass("nitidez", PassStage::Effects, out.texture, ShaderId::effects_sharpen_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_blur_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<GaussianBlur>());
    (void)r.add(std::make_unique<Sharpen>());
}

} // namespace aurea::builtin
