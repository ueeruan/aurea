// Threshold, Posterize e Palettize (Cor e luz). Só mudam as cores: nenhum
// deles pixela a imagem (o 8-Bit continua sendo o efeito que pixela).
// Tudo por pixel na GPU; o pontilhado por difusão do Palettize segue a
// mesma frente de onda x+3y do PixDither (um grupo de computação).
#include "BuiltinEffects.hpp"
#include "aurea/effects/Palette.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace aurea::builtin {
namespace {
struct TwoColorUniforms { EffectUniforms base; Vec4 second; };

f32 normalized_percent(const EffectEval& effect, u32 index, f32 fallback, f32 lo, f32 hi) noexcept {
    const f32 value = effect.f(index);
    return std::clamp(std::isfinite(value) ? value : fallback, lo, hi) * 0.01f;
}

Status passthrough_pass(EffectBuildContext& ctx, const char* name, ShaderId shader, const LayerImage& input,
                        LayerImage& out, const void* u, usize size, std::initializer_list<PassTexture> textures) {
    out = input; out.texture = ctx.texture(name, input.width, input.height);
    return ctx.fullscreen_pass(name, PassStage::Effects, out.texture, shader, textures, u, size) == kInvalidIndex
        ? Status{Errc::PipelineCompileFailed} : OkStatus;
}

// -----------------------------------------------------------------------------
// Threshold — duas cores pelo brilho, com borda suave opcional.
// -----------------------------------------------------------------------------
class Threshold final : public Effect {
public:
    enum : u32 { kLimit = 0, kSoftness, kDark, kLight };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.threshold", "Threshold", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("limit", "Limite", 50, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("softness", "Suavidade", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_color("dark", "Cor escura", Vec4{0, 0, 0, 1});
        p.add_color("light", "Cor clara", Vec4{1, 1, 1, 1});
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_color_threshold_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        TwoColorUniforms u{};
        u.base = base_uniforms(input);
        // NaN (expressão) não passa: std::clamp de NaN devolve NaN.
        const f32 limit = std::isfinite(e.f(kLimit)) ? e.f(kLimit) : 50.0f;
        const f32 soft = std::isfinite(e.f(kSoftness)) ? e.f(kSoftness) : 0.0f;
        u.base.p0 = {std::clamp(limit * .01f, 0.f, 1.f), std::clamp(soft * .01f, 0.f, 1.f), 0, 0};
        u.base.color = e.color(kDark); u.second = e.color(kLight);
        return passthrough_pass(ctx, "threshold", ShaderId::effects_color_threshold_frag, input, out, &u, sizeof(u),
                                {PassTexture{input.texture}});
    }
};

class SoftRgbThreshold final : public Effect {
public:
    enum : u32 { kLimit, kSoftness, kOffset, kSoftRed, kSoftGreen, kSoftBlue, kSaturation };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{"aurea.color.soft_rgb_threshold", "Limiar suave RGB", "Cor",
                                     EffectClass::Neighborhood};
        return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        constexpr auto percent = kParamAnimatable | kParamPercent;
        p.add_float("limit", "Limite", 50.0f, 0.0f, 200.0f, percent, "%");
        p.add_float("softness", "Suavidade", 20.0f, 0.0f, 100.0f, percent, "%");
        p.add_color("offset", "Cor de deslocamento", Vec4{0, 0, 0, 1});
        p.add_float("soft_red", "Suavidade do vermelho", 100.0f, 0.0f, 800.0f, percent, "%");
        p.add_float("soft_green", "Suavidade do verde", 100.0f, 0.0f, 800.0f, percent, "%");
        p.add_float("soft_blue", "Suavidade do azul", 100.0f, 0.0f, 800.0f, percent, "%");
        p.add_float("saturation", "Saturação", 100.0f, 0.0f, 400.0f, percent, "%");
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_color_threshold_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        TwoColorUniforms u{};
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{normalized_percent(e, kLimit, 50.0f, 0.0f, 200.0f),
                         normalized_percent(e, kSoftness, 20.0f, 0.0f, 100.0f), 1.0f,
                         normalized_percent(e, kSaturation, 100.0f, 0.0f, 400.0f)};
        u.base.p1 = Vec4{normalized_percent(e, kSoftRed, 100.0f, 0.0f, 800.0f),
                         normalized_percent(e, kSoftGreen, 100.0f, 0.0f, 800.0f),
                         normalized_percent(e, kSoftBlue, 100.0f, 0.0f, 800.0f), 0.0f};
        u.base.color = e.color(kOffset);
        return passthrough_pass(ctx, "limiar-suave-rgb", ShaderId::effects_color_threshold_frag, input, out,
                                &u, sizeof(u), {PassTexture{input.texture}});
    }
};

class Solarize final : public Effect {
public:
    enum : u32 { kThreshold, kSaturation, kInvert, kBrightness, kShadows };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{"aurea.color.solarize", "Solarizar", "Cor", EffectClass::Neighborhood};
        return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        constexpr auto percent = kParamAnimatable | kParamPercent;
        p.add_float("threshold", "Limite", 50.0f, 0.0f, 100.0f, percent, "%");
        p.add_float("saturation", "Saturação", 100.0f, -400.0f, 800.0f, percent, "%");
        p.add_bool("invert", "Inverter", false);
        p.add_float("brightness", "Brilho", 200.0f, 0.0f, 800.0f, percent, "%");
        p.add_float("shadows", "Sombras", 0.0f, -100.0f, 200.0f, percent, "%");
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_color_threshold_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        TwoColorUniforms u{};
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{normalized_percent(e, kThreshold, 50.0f, 0.0f, 100.0f),
                         normalized_percent(e, kSaturation, 100.0f, -400.0f, 800.0f), 2.0f,
                         e.b(kInvert) ? 1.0f : 0.0f};
        u.base.p1 = Vec4{normalized_percent(e, kShadows, 0.0f, -100.0f, 200.0f),
                         normalized_percent(e, kBrightness, 200.0f, 0.0f, 800.0f), 0.0f, 0.0f};
        return passthrough_pass(ctx, "solarizar", ShaderId::effects_color_threshold_frag, input, out,
                                &u, sizeof(u), {PassTexture{input.texture}});
    }
};

class EncodedFilmColor final : public Effect {
public:
    explicit EncodedFilmColor(bool palette) : palette_(palette) {}

    const EffectInfo& info() const noexcept override {
        static const EffectInfo colors{"aurea.color.luma_hue_cycle", "Ciclo de cores por brilho", "Cor",
                                       EffectClass::Neighborhood};
        static const EffectInfo silver{"aurea.color.silver_retention", "Retenção de prata", "Cor",
                                       EffectClass::Neighborhood};
        return palette_ ? colors : silver;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        constexpr auto percent = kParamAnimatable | kParamPercent;
        if (palette_) {
            p.add_float("frequency", "Frequência", 2.0f, 0.0f, 20.0f, kParamAnimatable);
            p.add_float("phase", "Deslocamento de matiz", 0.0f, -100.0f, 100.0f, percent, "%");
            p.add_float("scale_source", "Escalar pelo brilho", 100.0f, 0.0f, 100.0f, percent, "%");
            p.add_float("source_gamma", "Expoente do brilho", 1.0f, 0.1f, 8.0f, kParamAnimatable);
        } else {
            p.add_float("amount", "Intensidade", 100.0f, 0.0f, 300.0f, percent, "%");
            p.add_float("dark_offset", "Deslocamento das sombras", 0.0f, -100.0f, 100.0f, percent, "%");
        }
        p.add_float("saturation", "Saturação", 100.0f, -200.0f, 800.0f, percent, "%");
        p.add_float("brightness", "Brilho", 100.0f, 0.0f, 400.0f, percent, "%");
        p.add_float("original_mix", "Mistura com o original", 0.0f, 0.0f, 100.0f, percent, "%");
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_color_threshold_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        TwoColorUniforms u{};
        u.base = base_uniforms(input);
        const u32 tail = palette_ ? 4 : 2;
        u.base.p0 = Vec4{palette_ ? std::clamp(std::isfinite(e.f(0)) ? e.f(0) : 2.0f, 0.0f, 20.0f)
                                  : normalized_percent(e, 0, 100.0f, 0.0f, 300.0f),
                         normalized_percent(e, 1, 0.0f, -100.0f, 100.0f),
                         palette_ ? 3.0f : 4.0f, normalized_percent(e, tail, 100.0f, -200.0f, 800.0f)};
        u.base.p1 = Vec4{normalized_percent(e, tail + 1, 100.0f, 0.0f, 400.0f),
                         normalized_percent(e, tail + 2, 0.0f, 0.0f, 100.0f),
                         palette_ ? normalized_percent(e, 2, 100.0f, 0.0f, 100.0f) : 0.0f,
                         palette_ && std::isfinite(e.f(3)) ? std::clamp(e.f(3), 0.1f, 8.0f) : 1.0f};
        return passthrough_pass(ctx, palette_ ? "ciclo-de-cores" : "retencao-de-prata",
                                ShaderId::effects_color_threshold_frag, input, out,
                                &u, sizeof(u), {PassTexture{input.texture}});
    }

private:
    bool palette_;
};

// -----------------------------------------------------------------------------
// Posterize — menos tons por canal (ou só no brilho, em Oklab).
// -----------------------------------------------------------------------------
class Posterize final : public Effect {
public:
    enum : u32 { kLevels = 0, kGamma, kLumaOnly };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.posterize", "Posterize", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("levels", "Níveis", 6, 2, 32, kParamAnimatable);
        p.add_float("gamma", "Gamma", 1, 0.1f, 4, kParamAnimatable);
        p.add_bool("luma_only", "Só no brilho", false);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_color_posterize_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        // Níveis inteiros (keyframes passam de um nível ao outro sem meio-termo).
        const f32 levels = std::isfinite(e.f(kLevels)) ? e.f(kLevels) : 6.0f;
        const f32 gamma = std::isfinite(e.f(kGamma)) ? e.f(kGamma) : 1.0f;
        u.p0 = {std::round(std::clamp(levels, 2.f, 32.f)), std::clamp(gamma, 0.1f, 4.f), e.b(kLumaOnly) ? 1.f : 0.f, 0};
        return passthrough_pass(ctx, "posterize", ShaderId::effects_color_posterize_frag, input, out, &u, sizeof(u),
                                {PassTexture{input.texture}});
    }
};

class LumaPosterization final : public Effect {
public:
    enum : u32 { kStep, kPreserve, kSoftEdge, kSmooth, kPhase, kBrightness, kShadows, kSaturation };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{"aurea.color.luma_posterize", "Posterização por brilho", "Cor",
                                     EffectClass::Neighborhood};
        return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        constexpr auto percent = kParamAnimatable | kParamPercent;
        p.add_float("step", "Tamanho do degrau", 10.f, 0.f, 100.f, percent, "%");
        p.add_float("preserve_chroma", "Preservar croma", 100.f, 0.f, 100.f, percent, "%");
        p.add_float("smooth_edges", "Suavidade da borda", 10.f, 0.f, 100.f, percent, "%");
        p.add_float("smooth_source", "Suavizar fonte", 1.92f, 0.f, 80.f, kParamAnimatable | kParamPixels, "px");
        p.add_float("phase", "Fase da cor", 0.f, -100.f, 100.f, percent, "%");
        p.add_float("brightness", "Brilho", 100.f, 0.f, 400.f, percent, "%");
        p.add_float("shadows", "Sombras", 0.f, -100.f, 100.f, percent, "%");
        p.add_float("saturation", "Saturação", 100.f, -200.f, 800.f, percent, "%");
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_color_posterize_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        auto uniforms = base_uniforms(input);
        LayerImage signal;
        uniforms.p0.w = 1.f;
        const ShaderId shader = ShaderId::effects_color_posterize_frag;
        auto status = passthrough_pass(ctx, "posterizacao-codificar", shader, input, signal,
                                      &uniforms, sizeof(uniforms), {PassTexture{input.texture}});
        if (!status.ok()) return status;
        const f32 smooth = std::clamp(std::isfinite(e.f(kSmooth)) ? e.f(kSmooth) : 1.92f, 0.f, 80.f);
        LayerImage filtered = signal;
        if (smooth > 0.02f) {
            BlurRequest request{};
            request.sigmaX = request.sigmaY = smooth * 0.63f;
            request.repeatEdges = true;
            request.outRegion = signal.region;
            request.label = "posterizacao-suavizar";
            status = build_gaussian(ctx, signal, request, filtered);
            if (!status.ok()) return status;
        }
        uniforms.p0 = Vec4{normalized_percent(e, kStep, 10.f, 0.f, 100.f),
                           normalized_percent(e, kSoftEdge, 10.f, 0.f, 100.f),
                           normalized_percent(e, kPreserve, 100.f, 0.f, 100.f), 2.f};
        uniforms.p1 = Vec4{normalized_percent(e, kPhase, 0.f, -100.f, 100.f),
                           normalized_percent(e, kBrightness, 100.f, 0.f, 400.f),
                           normalized_percent(e, kShadows, 0.f, -100.f, 100.f),
                           normalized_percent(e, kSaturation, 100.f, -200.f, 800.f)};
        return passthrough_pass(ctx, "posterizacao-luma", shader, input, out, &uniforms, sizeof(uniforms),
                                {PassTexture{filtered.texture}, PassTexture{input.texture}});
    }
};

// -----------------------------------------------------------------------------
// Palettize — cada pixel vira a cor mais próxima (em Oklab) da paleta.
// -----------------------------------------------------------------------------
constexpr u32 kImportedPalette = 5;

const std::vector<Vec3>& builtin_palette(u32 index) {
    auto hex = [](std::initializer_list<u32> list) {
        std::vector<Vec3> out;
        for (u32 c : list) out.push_back(Vec3{((c >> 16) & 255u) / 255.f, ((c >> 8) & 255u) / 255.f, (c & 255u) / 255.f});
        return out;
    };
    // Valores públicos de hardware (fatos de RGB), escritos aqui.
    static const std::array<std::vector<Vec3>, 5> palettes{
        hex({0x0F380F, 0x306230, 0x8BAC0F, 0x9BBC0F}),
        hex({0x7C7C7C, 0x0000FC, 0x0000BC, 0x4428BC, 0x940084, 0xA80020, 0xA81000, 0x881400, 0x503000, 0x007800, 0x006800,
             0x005800, 0x004058, 0x000000, 0xBCBCBC, 0x0078F8, 0x0058F8, 0x6844FC, 0xD800CC, 0xE40058, 0xF83800, 0xE45C10,
             0xAC7C00, 0x00B800, 0x00A800, 0x00A844, 0x008888, 0xF8F8F8, 0x3CBCFC, 0x6888FC, 0x9878F8, 0xF878F8, 0xF85898,
             0xF87858, 0xFCA044, 0xF8B800, 0xB8F818, 0x58D854, 0x58F898, 0x00E8D8, 0x787878, 0xFCFCFC, 0xA4E4FC, 0xB8B8F8,
             0xD8B8F8, 0xF8B8F8, 0xF8A4C0, 0xF0D0B0, 0xFCE0A8, 0xF8D878, 0xD8F878, 0xB8F8B8, 0xB8F8D8, 0x00FCFC, 0xF8D8F8}),
        hex({0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8, 0xFF004D, 0xFFA300, 0xFFEC27,
             0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA}),
        hex({0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA, 0x555555, 0x5555FF, 0x55FF55,
             0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF}),
        hex({0x000000, 0xFFFFFF}),
    };
    return palettes[std::min<u32>(index, 4)];
}

const CubeLut& builtin_table(u32 index) {
    static const std::array<CubeLut, 5> tables{palette_table(builtin_palette(0)), palette_table(builtin_palette(1)),
        palette_table(builtin_palette(2)), palette_table(builtin_palette(3)), palette_table(builtin_palette(4))};
    return tables[std::min<u32>(index, 4)];
}

class Palettize final : public Effect {
public:
    enum : u32 { kPalette = 0, kFile, kDither, kDitherAmount, kAmount };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.palettize", "Palettize", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const palettes[] = {"Game Boy", "NES", "PICO-8", "CGA", "Preto e branco", "Importada"};
        // Os mesmos algoritmos do PixDither (o índice é o mesmo; 0 = nenhum).
        static const char* const methods[] = {"Nenhum", "Floyd Steinberg", "False Floyd Steinberg", "Filter Lite", "Zhigang Fan",
            "Shiau-Fan", "Jarvis, Judice & Ninke", "Stucki", "Burkes", "Sierra", "Two Row Sierra", "Atkinson", "Bayer 2x2",
            "Bayer 3x3", "Bayer 5x3", "Bayer 4x4", "Bayer 8x8", "Cluster Dot 4x4", "Cluster Dot 8x8", "Horizontal 2x2",
            "Horizontal 8x1", "Horizontal 12x4", "Vertical 2x2", "Vertical 1x8", "Vertical 4x12", "Diagonal 5x5"};
        p.add_enum("palette", "Paleta", palettes, 6, 0);
        p.add_texture_ref("file", "Arquivo da paleta");
        p.add_enum("dither", "Pontilhado", methods, 26, 0);
        p.add_float("dither_amount", "Força do pontilhado", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("amount", "Intensidade", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
    }
    bool needs_full_input() const noexcept override { return true; }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kAmount) <= 0; }
    void resolve_resources(EffectEval& e) const noexcept override {
        if (!e.resources) return;
        const u32 palette = e.e(kPalette);
        if (palette == kImportedPalette) {
            if (e.value(kFile).ref) e.aux = e.resources->cube_lut(AssetId::unpack(e.value(kFile).ref));
            return;
        }
        const CubeLut& t = builtin_table(palette);
        e.aux = e.resources->data_texture(0x50414c4554544500ull | palette, t.values.data(), static_cast<u32>(t.values.size()), 1);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_color_palettize_frag, work));
        out.push_back(PipelineKey::compute_shader(ShaderId::effects_color_palettize_diffusion_comp));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        // Paleta importada que ainda não chegou (ou foi apagada): a imagem passa como está.
        if (!e.aux.valid()) { out = input; return OkStatus; }
        const u32 method = e.e(kDither);
        EffectUniforms u = base_uniforms(input);
        u.p0 = {static_cast<f32>(method), std::clamp(e.f(kDitherAmount) * .01f, 0.f, 1.f), std::clamp(e.f(kAmount) * .01f, 0.f, 1.f), 0};
        u.p1 = {static_cast<f32>(input.width), static_cast<f32>(input.height), 0, 0};
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        if (method == 0 || method >= 12 || u.p0.y <= 0) {
            return passthrough_pass(ctx, "palettize", ShaderId::effects_color_palettize_frag, input, out, &u, sizeof(u),
                {PassTexture{input.texture, {}, CommonSampler::NearestClamp}, PassTexture{{}, e.aux, CommonSampler::NearestClamp}});
        }
        auto pipeline = ctx.shaders().pipeline(PipelineKey::compute_shader(ShaderId::effects_color_palettize_diffusion_comp));
        if (!pipeline.ok()) return pipeline.status();
        TextureDesc description;
        description.width = input.width; description.height = input.height; description.sampled = true; description.storage = true;
        description.format = SurfaceFormat::RGBA32F;
        const FGTexture errors = ctx.graph().create_texture("palettize-errors", description);
        description.format = SurfaceFormat::RGBA16F;
        out = input; out.texture = ctx.graph().create_texture("palettize-diffusion", description);
        const auto sampler = ctx.shaders().sampler(CommonSampler::NearestClamp);
        const auto target = out.texture, source = input.texture;
        const TextureHandle table = e.aux;
        const auto pipe = *pipeline;
        const u32 pass = ctx.graph().add_compute_pass("palettize-diffusion", PassStage::Effects,
            [u, source, target, errors, table, sampler, pipe](PassContext& pc) {
                pc.cmds.bind_pipeline(pipe);
                pc.cmds.bind_texture(0, pc.texture(source), sampler);
                pc.cmds.bind_texture(1, table, sampler);
                pc.cmds.bind_storage_image(0, pc.texture(errors));
                pc.cmds.bind_storage_image(1, pc.texture(target));
                pc.cmds.set_uniforms(&u, sizeof(u));
                pc.cmds.dispatch(1, 1, 1);
            });
        ctx.graph().read(pass, source);
        ctx.graph().write_storage(pass, errors);
        ctx.graph().write_storage(pass, target);
        return OkStatus;
    }
};
} // namespace

void register_color_quantize_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Threshold>());
    (void)r.add(std::make_unique<SoftRgbThreshold>());
    (void)r.add(std::make_unique<Solarize>());
    (void)r.add(std::make_unique<EncodedFilmColor>(true));
    (void)r.add(std::make_unique<EncodedFilmColor>(false));
    (void)r.add(std::make_unique<Posterize>());
    (void)r.add(std::make_unique<LumaPosterization>());
    (void)r.add(std::make_unique<Palettize>());
}
}
