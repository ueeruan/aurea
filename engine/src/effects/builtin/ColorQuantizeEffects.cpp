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
        u.base.p0 = {std::clamp(e.f(kLimit) * .01f, 0.f, 1.f), std::clamp(e.f(kSoftness) * .01f, 0.f, 1.f), 0, 0};
        u.base.color = e.color(kDark); u.second = e.color(kLight);
        return passthrough_pass(ctx, "threshold", ShaderId::effects_color_threshold_frag, input, out, &u, sizeof(u),
                                {PassTexture{input.texture}});
    }
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
        u.p0 = {std::round(std::clamp(e.f(kLevels), 2.f, 32.f)), std::clamp(e.f(kGamma), 0.1f, 4.f), e.b(kLumaOnly) ? 1.f : 0.f, 0};
        return passthrough_pass(ctx, "posterize", ShaderId::effects_color_posterize_frag, input, out, &u, sizeof(u),
                                {PassTexture{input.texture}});
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
    (void)r.add(std::make_unique<Posterize>());
    (void)r.add(std::make_unique<Palettize>());
}
}
