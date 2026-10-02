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
    // Camada na cena 3D: a posição dela é o plano no mundo, não a matriz 2D da
    // composição — cortar por ela encolheria a região para 1x1 e a camada
    // desapareceria atrás do efeito.
    if (placement && !placement->inScene3d && placement->compWidth && placement->compHeight) {
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
        gaussian_pairs(sigma, u);
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
    enum : u32 { kAmount = 0 };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSharpen, "Nitidez", "Desfoque", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
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

// -----------------------------------------------------------------------------
// Desfoque de zoom — rastro radial: cada pixel é a média de amostras tiradas
// ao longo da reta que liga o pixel ao centro. É o "empurrar a lente": as
// bordas correm mais que o centro, porque o passo é proporcional à distância
// até ele.
// -----------------------------------------------------------------------------
class ZoomBlur final : public Effect {
public:
    enum : u32 { kCenter = 0, kAmount, kRepeat, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kZoomBlur, "Desfoque de zoom", "Desfoque",
                                  EffectClass::Domain};
        return i;
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
        const Vec2 c = e.p2(kCenter);
        u.p0 = Vec4{c.x, c.y, std::clamp(e.f(kAmount) * 0.01f, -4.0f, 4.0f) * 0.5f,
                    e.b(kRepeat) ? 1.0f : 0.0f};
        // A mistura com o original: o rastro nunca é o único resultado.
        const f32 k = std::clamp(e.f(kMix) * 0.01f, 0.0f, 1.0f);
        u.p1 = Vec4{k, 0, 0, 0};
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
};

} // namespace

void register_blur_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<GaussianBlur>());
    (void)r.add(std::make_unique<Sharpen>());
    (void)r.add(std::make_unique<ZoomBlur>());
}

} // namespace aurea::builtin
