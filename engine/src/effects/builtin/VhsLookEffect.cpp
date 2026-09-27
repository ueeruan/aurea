// =============================================================================
//  VHS (Estilizar) — o visual de fita do tutorial clássico, pronto de fábrica.
//
//  Tom quente e desbotado, luma suave, croma escorrendo para a direita, ruído
//  fino, varredura, faixas de tracking com chuvisco, tremor por linha, vinheta
//  e o OSD do videocassete ("PLAY ▶", SP, tempo hh:mm:ss da camada) desenhado
//  no próprio shader. Um passe só, oito leituras horizontais.
//
//  O VHS da categoria Glitch (aurea.glitch.vhs) é o defeito de fita com
//  controles de dano; este é o LOOK — os padrões já dão a cara de fita.
//
//  O tempo do OSD é o tempo LOCAL da camada (quadro / fps), calculado aqui em
//  dígitos inteiros: o shader só desenha. Tudo que se mexe (ruído, tremor,
//  faixas) é sorteado pelo quadro e pela semente — o mesmo quadro dá os
//  mesmos bits no preview, no scrubbing e no export.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

Vec2 layer_size(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight) {
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    }
    return Vec2{input.region.w, input.region.h};
}

/// O bloco de uniforms: o comum mais quatro vec4 (como os geradores).
struct VhsUniforms {
    EffectUniforms base{};
    Vec4 q0{};
    Vec4 q1{};
    Vec4 q2{};
    Vec4 q3{};
};
static_assert(sizeof(VhsUniforms) == 176, "layout std140 dos uniforms do VHS");

class VhsLook final : public Effect {
public:
    enum : u32 { kIntensity = 0, kBleed, kSoften, kNoise, kScanlines, kTracking, kJitter, kTone,
                 kSaturation, kVignette, kOverlay, kShowTime, kLabel, kOsdColor, kOsdSize, kSeed };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kVhsLook, "VHS", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kOverlays[] = {"Nenhuma", "PLAY", "REC", "PAUSE"};
        static const char* const kLabels[] = {"SP", "LP", "EP"};
        p.add_float("intensity", "Intensidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        // Comprimentos em px da camada: o slider cobre o uso normal num 1080p,
        // a digitação vai longe para camadas enormes.
        p.add_float("chroma_bleed", "Sangramento de cor", 9.0f, 0.0f, 40.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 400.0f);
        p.add_float("soften", "Suavização", 2.5f, 0.0f, 20.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 200.0f);
        p.add_float("noise", "Ruído", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("scanlines", "Linhas de varredura", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("tracking", "Tracking", 40.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("jitter", "Tremor", 1.5f, 0.0f, 20.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 200.0f);
        // 0 = neutro; 100 = preto quente levantado e branco creme.
        p.add_float("tone", "Tom", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("saturation", "Saturação", 85.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("vignette", "Vinheta", 40.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_enum("overlay", "Sobreposição", kOverlays, 4, 1);
        p.add_bool("show_time", "Mostrar tempo", true);
        p.add_enum("label", "Rótulo", kLabels, 3, 0);
        p.add_color("osd_color", "Cor do OSD", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("osd_size", "Tamanho do OSD", 100.0f, 25.0f, 300.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_int("seed", "Semente", 1, 0, 9999);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kIntensity), 0.0f) > 0.01f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::clamp(finite_or(e.f(kBleed), 0.0f), 0.0f, 400.0f)
             + std::clamp(finite_or(e.f(kSoften), 0.0f), 0.0f, 200.0f)
             + std::clamp(finite_or(e.f(kJitter), 0.0f), 0.0f, 200.0f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_vhs_look_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 size = layer_size(e, input);
        // Tempo local da camada em segundos inteiros → hh:mm:ss (horas até 99).
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        const f64 local = std::max<f64>(0.0, static_cast<f64>(e.localTime.value));
        const f64 seconds = local / fps;
        const i64 whole = std::min<i64>(static_cast<i64>(std::floor(seconds + 1e-6)), 99 * 3600 + 59 * 60 + 59);
        const f32 hh = static_cast<f32>(whole / 3600);
        const f32 mm = static_cast<f32>((whole / 60) % 60);
        const f32 ss = static_cast<f32>(whole % 60);
        // Pixel da fonte: 7 linhas ≈ 5,6% da altura da camada no tamanho 100%.
        const f32 osdSize = std::clamp(finite_or(e.f(kOsdSize), 100.0f), 0.0f, 1000.0f) / 100.0f;
        const f32 fontPx = size.y * 0.008f * osdSize;

        VhsUniforms u;
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{std::clamp(finite_or(e.f(kIntensity), 100.0f) / 100.0f, 0.0f, 1.0f),
                         std::clamp(finite_or(e.f(kBleed), 0.0f), 0.0f, 400.0f),
                         std::clamp(finite_or(e.f(kSoften), 0.0f), 0.0f, 200.0f),
                         std::clamp(finite_or(e.f(kNoise), 0.0f) / 100.0f, 0.0f, 1.0f)};
        u.base.p1 = Vec4{std::clamp(finite_or(e.f(kScanlines), 0.0f) / 100.0f, 0.0f, 1.0f),
                         std::clamp(finite_or(e.f(kTracking), 0.0f) / 100.0f, 0.0f, 1.0f),
                         std::clamp(finite_or(e.f(kJitter), 0.0f), 0.0f, 200.0f),
                         std::clamp(finite_or(e.f(kTone), 0.0f) / 100.0f, 0.0f, 1.0f)};
        u.base.p2 = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        u.base.p3 = Vec4{size.x, size.y, static_cast<f32>(local), static_cast<f32>(seconds)};
        u.base.color = e.color(kOsdColor);
        u.q0 = Vec4{std::clamp(finite_or(e.f(kSaturation), 100.0f) / 100.0f, 0.0f, 2.0f),
                    std::clamp(finite_or(e.f(kVignette), 0.0f) / 100.0f, 0.0f, 1.0f),
                    static_cast<f32>(std::min<u32>(e.e(kOverlay), 3u)), e.b(kShowTime) ? 1.0f : 0.0f};
        u.q1 = Vec4{static_cast<f32>(std::min<u32>(e.e(kLabel), 2u)), fontPx,
                    static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 9999)), input.texel_scale_y()};
        u.q2 = Vec4{hh, mm, ss, 0.0f};

        out = input;
        out.texture = ctx.texture("vhs-look", input.width, input.height);
        if (ctx.fullscreen_pass("vhs-look", PassStage::Effects, out.texture, ShaderId::effects_vhs_look_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_vhs_look_effect(EffectRegistry& r) {
    (void)r.add(std::make_unique<VhsLook>());
}

} // namespace aurea::builtin
