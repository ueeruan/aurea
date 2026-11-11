// =============================================================================
//  Aurea / effects / builtin / TextureOverlayEffect.cpp
//
//  0.5.2: TEXTURA (`aurea.stylize.texture_overlay`). Uma textura por cima de
//  qualquer camada — papel, papel amassado, grão de filme, poeira e
//  arranhões, concreto, lona, grunge, meio-tom, ruído, vidro fosco, pedra,
//  madeira, metal escovado, couro — ou a imagem de outra camada (a foto da
//  galeria posta na timeline, como no Texturizar). Modo de mistura,
//  opacidade, escala, rotação, animação, relevo e "só dentro da camada".
//
//  As texturas são as MESMAS do preenchimento com textura do texto: uma
//  biblioteca só (shaders/common/texture_lib.glsl). Não substitui Grão,
//  Meio-tom, Ruído fractal e Texturizar: esses PROCESSAM a imagem (o grão vive
//  nas sombras, o meio-tom reticula a cor da camada); aqui a textura é uma
//  folha sobreposta, independente do conteúdo.
//
//  A ordem dos parâmetros é contrato (projeto salvo e i18n): só acrescentar
//  no fim.
// =============================================================================
#include "AeEffects2026.hpp"

#include "aurea/text/TextTexture.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

using namespace ae26;

constexpr const char* kTextureOverlayKey = "aurea.stylize.texture_overlay";

/// As escolhas do parâmetro "Textura", na ordem do enum, e o gerador de cada
/// uma em common/texture_lib.glsl (0 = a imagem da camada escolhida).
constexpr u8 kPresetGenerator[] = {
    static_cast<u8>(ProceduralTexture::Paper),
    static_cast<u8>(ProceduralTexture::CrumpledPaper),
    static_cast<u8>(ProceduralTexture::FilmGrain),
    static_cast<u8>(ProceduralTexture::DustScratches),
    static_cast<u8>(ProceduralTexture::Concrete),
    static_cast<u8>(ProceduralTexture::Canvas),
    static_cast<u8>(ProceduralTexture::Grunge),
    static_cast<u8>(ProceduralTexture::Halftone),
    static_cast<u8>(ProceduralTexture::Noise),
    static_cast<u8>(ProceduralTexture::FrostedGlass),
    static_cast<u8>(ProceduralTexture::Stone),
    static_cast<u8>(ProceduralTexture::Wood),
    static_cast<u8>(ProceduralTexture::BrushedMetal),
    static_cast<u8>(ProceduralTexture::Leather),
    0,   // Imagem da galeria (camada)
};
constexpr u32 kPresetCount = sizeof(kPresetGenerator) / sizeof(kPresetGenerator[0]);
constexpr u32 kImagePreset = kPresetCount - 1;

class TextureOverlay final : public Effect {
public:
    enum : u32 { kPreset = 0, kBlend, kOpacity, kScale, kRotation, kSpeed, kInside, kRelief, kContrast, kSeed, kLayer };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kTextureOverlayKey, "Textura", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kPresets[] = {
            "Papel", "Papel amassado", "Grão de filme", "Poeira e arranhões", "Concreto", "Tecido (lona)", "Grunge",
            "Meio-tom", "Ruído", "Vidro fosco", "Pedra", "Madeira", "Metal escovado", "Couro", "Imagem da galeria",
        };
        static_assert(sizeof(kPresets) / sizeof(kPresets[0]) == kPresetCount);
        static const char* const kBlends[] = {"Sobrepor", "Multiplicar", "Tela", "Luz suave", "Normal", "Adicionar"};
        p.add_enum("texture", "Textura", kPresets, kPresetCount, 0);
        p.add_enum("blend_mode", "Modo de mistura", kBlends, 6, 0);
        p.add_float("opacity", "Opacidade", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("scale", "Escala", 100.0f, 10.0f, 1000.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_float("speed", "Animar (velocidade)", 1.0f, 0.0f, 10.0f, kParamAnimatable);
        p.add_bool("inside_alpha", "Só dentro da camada", true);
        p.add_float("relief", "Relevo", 40.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("contrast", "Contraste", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Variação", 0, 0, 9999);
        p.add_layer_ref("texture_layer", "Camada da imagem");
    }
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kLayer); }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (!(fin(e.f(kOpacity), 0.0f) > 0.05f)) return true;
        // "Imagem da galeria" sem camada escolhida: nada a sobrepor.
        return std::min<u32>(e.e(kPreset), kPresetCount - 1) == kImagePreset && e.value(kLayer).ref == 0;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::min<u32>(e.e(kPreset), kPresetCount - 1) == 9 ? 8.0f : 0.0f;   // vidro fosco desloca
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kPreset] = ParamValue::scalar(6.0f);   // Grunge: a diferença aparece na cartela pequena
        v[kOpacity] = ParamValue::scalar(85.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_texture_overlay_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        const u32 preset = std::min<u32>(e.e(kPreset), kPresetCount - 1);
        const u64 ref = e.value(kLayer).ref;
        const LayerImage* tex = preset == kImagePreset && ref ? ctx.layer_input(ref) : nullptr;
        if (preset == kImagePreset && !tex) { out = input; return OkStatus; }
        AeUniforms u = ae_uniforms(e, input);
        const Vec2 ls = layer_size(e, input);
        // 1 unidade de textura = 1/5 da média geométrica da camada × escala:
        // a mesma trama no preview reduzido e no export (px da camada).
        const f32 unit = std::max(1.0f, std::sqrt(std::max(ls.x, 1.0f) * std::max(ls.y, 1.0f)) * 0.2f
                                        * clampf(e.f(kScale), 10.0f, 1000.0f, 100.0f) * 0.01f);
        const f32 speed = clampf(e.f(kSpeed), 0.0f, 10.0f, 1.0f);
        const f32 phase = static_cast<f32>(std::fmod(seconds(e) * static_cast<f64>(speed), 3600.0));
        u.p[0] = Vec4{static_cast<f32>(kPresetGenerator[preset]), en(e, kBlend, 6), pct(e, kOpacity), unit};
        u.p[1] = Vec4{fin(e.f(kRotation), 0.0f) * kDeg2Rad, phase, flag(e.b(kInside)), pct(e, kRelief)};
        u.p[2] = Vec4{pct(e, kContrast, -1.0f, 1.0f), static_cast<f32>(std::min<u32>(e.e(kSeed), 9999u)), tex ? 1.0f : 0.0f,
                      static_cast<f32>(preset == 9 ? 1 : 0)};
        // Região da imagem da outra camada (px da composição): ela é o ladrilho.
        const Rect tr = tex ? tex->region : input.region;
        u.p[3] = Vec4{tr.x, tr.y, std::max(tr.w, 1.0f), std::max(tr.h, 1.0f)};
        const PassTexture tex1{tex ? tex->texture : input.texture, {}, CommonSampler::LinearRepeat};
        return ae_pass(ctx, input, u, ShaderId::effects_texture_overlay_frag, "textura-sobreposta", CommonSampler::LinearClamp,
                       out, &tex1, 1);
    }
};

} // namespace

void register_texture_overlay_effect(EffectRegistry& r) {
    (void)r.add(std::make_unique<TextureOverlay>());
}

} // namespace aurea::builtin
