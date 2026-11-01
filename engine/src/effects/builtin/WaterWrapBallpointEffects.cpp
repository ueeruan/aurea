// =============================================================================
//  Pedidos de usuários da 0.4.5: Reflexo na água · Envolver com luz · Caneta
//
//  Reflexo na água (Distorcer): a camada espelhada abaixo de uma linha do
//  horizonte, com ondulação animada (intensidade, frequência, velocidade,
//  direção, perspectiva), desfoque pela distância, tom da água e
//  desvanecimento — water_reflection.frag.
//
//  Envolver com luz (Luz; o "Light Wrap" dos compositores): a luz da camada de
//  fundo escolhida (ou de uma cor) vaza para dentro das bordas da camada, só
//  por dentro da silhueta — light_wrap.frag.
//
//  Caneta esferográfica (Transição; o "Ballpoint" dos pacotes de transição):
//  uma caneta rabisca a camada em linhas, revelando-a (ou apagando-a) —
//  ballpoint.frag.
//
//  Comportamento especificado por referências públicas (descrições e vídeos
//  desses efeitos nos editores de movimento); matemática e shaders nossos.
//  Tudo em px da camada pelo layout do pacote CC (cc_common.glsl): prévia em
//  1/4 e export em 4K desenham o mesmo lugar. Os índices de parâmetro ficam
//  fixos nos projetos (acrescentar só no fim).
// =============================================================================
#include "CCCommon.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
using namespace cc;

constexpr const char* kWaterReflectionKey = "aurea.distort.water_reflection";
constexpr const char* kLightWrapKey = "aurea.light.light_wrap";
constexpr const char* kBallpointKey = "aurea.transition.ballpoint";

// =============================================================================
// Reflexo na água
// =============================================================================
class WaterReflection final : public Effect {
public:
    enum : u32 { kHorizon = 0, kLength, kOpacity, kFalloff, kAmount, kFrequency, kSpeed, kDirection,
                 kPerspective, kBlur, kTint, kTintAmount, kCount };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kWaterReflectionKey, "Reflexo na água", "Distorcer", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // 100% = a borda de baixo da camada: o reflexo nasce embaixo dela (logo, texto).
        p.add_float("horizon", "Horizonte", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(-100.0f, 200.0f);
        p.add_float("length", "Comprimento do reflexo", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("opacity", "Opacidade do reflexo", 60.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("falloff", "Desvanecimento", 70.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("amount", "Ondulação", 6.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("frequency", "Frequência", 4.0f, 0.1f, 40.0f, kAnim);
        p.typed_range(0.01f, 400.0f);
        p.add_float("speed", "Velocidade", 1.0f, -10.0f, 10.0f, kAnim);
        p.typed_range(-100.0f, 100.0f);
        p.add_angle("direction", "Direção", 90.0f);
        p.add_float("perspective", "Perspectiva", 60.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("blur", "Desfoque", 2.0f, 0.0f, 40.0f, kAnimPx, "px");
        p.typed_range(0.0f, 400.0f);
        p.add_color("tint", "Cor da água", Vec4{0.25f, 0.55f, 0.72f, 1.0f});
        p.add_float("tint_amount", "Tom", 25.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.set_tip(kFrequency, "Ondas a cada 100 px.");
        p.set_tip(kSpeed, "Ondas por segundo. 0 = água parada.");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.count < kCount || !(finite_or(e.f(kOpacity), 60.0f) > 0.0f) || !(finite_or(e.f(kLength), 100.0f) > 0.0f);
    }
    // A prévia (foto que enche o quadro): a linha no meio, para o reflexo aparecer.
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHorizon] = ParamValue::scalar(55.0f);
        v[kOpacity] = ParamValue::scalar(80.0f);
        return true;
    }
    // O espelho lê a camada ACIMA da linha, que pode estar fora do quadro visível.
    bool needs_full_input() const noexcept override { return true; }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return clampf(e.f(kAmount), 0.0f, 1000.0f, 6.0f) + 1.4f * clampf(e.f(kBlur), 0.0f, 400.0f, 2.0f) + 2.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_water_reflection_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const f32 horizon = box.y + box.h * clampf(e.f(kHorizon), -100.0f, 200.0f, 100.0f) / 100.0f;
        const f32 length = box.h * clampf(e.f(kLength), 0.0f, 1000.0f, 100.0f) / 100.0f;
        const f32 amount = clampf(e.f(kAmount), 0.0f, 1000.0f, 6.0f);
        const f32 blur = clampf(e.f(kBlur), 0.0f, 400.0f, 2.0f);
        const f32 angle = finite_or(e.f(kDirection), 90.0f) * kDeg;
        constexpr f32 kTau = 6.28318530718f;
        Uniforms u{};
        u.p0 = Vec4{horizon, std::max(length, 1.0f), clampf(e.f(kOpacity), 0.0f, 100.0f, 60.0f) / 100.0f,
                    clampf(e.f(kFalloff), 0.0f, 100.0f, 70.0f) / 100.0f};
        // Frequência em ondas a cada 100 px; a fase anda `velocidade` voltas por segundo.
        const f32 phase = std::fmod(seconds(e) * clampf(e.f(kSpeed), -100.0f, 100.0f, 1.0f), 1000.0f) * kTau;
        u.p1 = Vec4{amount, kTau * clampf(e.f(kFrequency), 0.01f, 400.0f, 4.0f) / 100.0f, phase,
                    clampf(e.f(kPerspective), 0.0f, 100.0f, 60.0f) / 100.0f};
        u.p2 = Vec4{std::cos(angle), std::sin(angle), blur, clampf(e.f(kTintAmount), 0.0f, 100.0f, 25.0f) / 100.0f};
        u.color = premultiplied_linear(e.color(kTint));
        // A saída cresce para baixo até o fim do reflexo (e para os lados pela ondulação).
        const f32 side = amount + 1.4f * blur;
        const f32 x0 = std::min(in.region.x, in.region.x - side), x1 = in.region.x + in.region.w + side;
        const f32 y0 = in.region.y, y1 = std::max(in.region.y + in.region.h, horizon + length);
        const Rect want{x0, y0, x1 - x0, y1 - y0};
        return run_pass(ctx, e, ShaderId::effects_water_reflection_frag, in, grown_region(e, in.region, want, margin), u,
                        "reflexo-na-agua", out);
    }
};

// =============================================================================
// Envolver com luz (Light Wrap)
// =============================================================================
class LightWrap final : public Effect {
public:
    enum : u32 { kBackground = 0, kWidth, kIntensity, kBlend, kBackgroundBlur, kColor, kWrapOnly, kCount };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kLightWrapKey, "Envolver com luz", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // A ordem casa com o índice do aurea_blend_generated via kBlendCode.
        static const char* const kModes[] = {"Tela", "Adicionar", "Normal", "Sobrepor", "Multiplicar"};
        p.add_layer_ref("background", "Camada de fundo");
        p.add_float("width", "Largura", 20.0f, 1.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.5f, 2000.0f);
        p.add_float("intensity", "Intensidade", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.add_enum("blend", "Modo de mistura", kModes, 5, 0);
        p.add_float("background_blur", "Desfoque do fundo", 12.0f, 0.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_color("color", "Cor da luz", Vec4{1.0f, 0.86f, 0.62f, 1.0f});
        p.add_bool("wrap_only", "Só o envoltório", false);
        p.set_tip(kBackground, "Escolha a camada que fica atrás (a luz dela entra nas bordas). "
                               "Sem camada, a luz é a cor abaixo.");
    }
    i32 input_layer_param() const noexcept override { return kBackground; }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.count < kCount || (!(finite_or(e.f(kIntensity), 100.0f) > 0.0f) && !e.b(kWrapOnly));
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return 1.5f * clampf(e.f(kWidth), 0.5f, 2000.0f, 20.0f) + 2.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_light_wrap_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        static constexpr f32 kBlendCode[] = {2.0f, 4.0f, 0.0f, 3.0f, 1.0f};   // tela, adicionar, normal, sobrepor, multiplicar
        const f32 width = clampf(e.f(kWidth), 0.5f, 2000.0f, 20.0f);
        // O alfa desfocado diz quão perto da borda cada ponto está.
        LayerImage alpha;
        {
            BlurRequest req;
            req.sigmaX = req.sigmaY = 0.5f * width;
            req.repeatEdges = false;
            req.outRegion = in.region;
            req.label = "envolver-luz-alfa";
            if (const Status s = build_gaussian(ctx, in, req, alpha); !s.ok()) return s;
        }
        const u64 ref = e.value(kBackground).ref;
        const LayerImage* bg = ref ? ctx.layer_input(ref) : nullptr;
        LayerImage bgBlurred;
        if (bg && bg->valid()) {
            const f32 sigma = clampf(e.f(kBackgroundBlur), 0.0f, 2000.0f, 12.0f);
            if (sigma > 0.25f) {
                BlurRequest req;
                req.sigmaX = req.sigmaY = sigma;
                req.repeatEdges = true;
                req.outRegion = bg->region;
                req.label = "envolver-luz-fundo";
                if (const Status s = build_gaussian(ctx, *bg, req, bgBlurred); !s.ok()) return s;
            } else {
                bgBlurred = *bg;
            }
        }
        const bool hasBg = bgBlurred.valid();
        Uniforms u{};
        u.p0 = Vec4{clampf(e.f(kIntensity), 0.0f, 200.0f, 100.0f) / 100.0f, e.b(kWrapOnly) ? 1.0f : 0.0f,
                    kBlendCode[std::min<u32>(e.e(kBlend), 4u)], 0.0f};
        // px da camada → px da composição (a linha afim; a camada 2D comum).
        const Mat4 m = e.placement ? e.placement->compFromLayer : Mat4::identity();
        u.p1 = Vec4{m.col[0].x, m.col[1].x, m.col[3].x, hasBg ? 1.0f : 0.0f};
        u.p2 = Vec4{m.col[0].y, m.col[1].y, m.col[3].y, 0.0f};
        u.p3 = Vec4{alpha.region.x, alpha.region.y, alpha.region.w, alpha.region.h};
        u.p4 = hasBg ? Vec4{bgBlurred.region.x, bgBlurred.region.y, bgBlurred.region.w, bgBlurred.region.h}
                     : Vec4{0.0f, 0.0f, 1.0f, 1.0f};
        const Vec4 c = premultiplied_linear(e.color(kColor));
        u.color = Vec4{c.x, c.y, c.z, 1.0f};

        u32 w = 0, h = 0;
        ctx.region_size(in.region, in.texel_scale_x(), w, h);
        if (!w || !h) { out = in; return OkStatus; }
        const Rect region = in.region;
        u.dst = Vec4{region.x, region.y, region.w, region.h};
        u.src = u.dst;
        const Rect box = layer_box(e, in);
        u.box = Vec4{box.x, box.y, box.w, box.h};
        const f32 texel = 0.5f * (region.w / static_cast<f32>(w) + region.h / static_cast<f32>(h));
        u.info = Vec4{std::max(texel, 1e-4f), seconds(e), in.texel_scale_x(), 0.0f};
        out = LayerImage{ctx.texture("envolver-luz", w, h), region, w, h};
        const u32 pass = ctx.fullscreen_pass(
            "envolver-luz", PassStage::Effects, out.texture, ShaderId::effects_light_wrap_frag,
            {PassTexture{in.texture, {}, CommonSampler::LinearBorder},
             PassTexture{alpha.texture, {}, CommonSampler::LinearClamp},
             PassTexture{hasBg ? bgBlurred.texture : in.texture, {}, CommonSampler::LinearClamp}},
            &u, sizeof(u));
        return pass == kInvalidIndex ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};

// =============================================================================
// Caneta esferográfica (Ballpoint)
// =============================================================================
class Ballpoint final : public Effect {
public:
    enum : u32 { kProgress = 0, kMode, kAuto, kDirection, kRows, kStroke, kScribble, kJitter, kInkColor,
                 kInk, kFillDelay, kSeed, kCount };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kBallpointKey, "Caneta esferográfica", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kModes[] = {"Revelar camada", "Remover camada"};
        p.add_float("progress", "Progresso", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("transition_dir", "Direção", kModes, 2, 0);
        p.add_bool("auto_transition", "Transição automática", false);
        p.add_angle("direction", "Ângulo", 0.0f);
        p.add_float("rows", "Linhas", 6.0f, 1.0f, 40.0f, kAnim);
        p.typed_range(1.0f, 400.0f);
        p.add_float("stroke_width", "Espessura do traço", 6.0f, 0.5f, 60.0f, kAnimPx, "px");
        p.typed_range(0.5f, 600.0f);
        p.add_float("scribble", "Densidade do rabisco", 50.0f, 1.0f, 100.0f, kAnimPct, "%");
        p.add_float("jitter", "Tremor da mão", 40.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_color("ink_color", "Cor da tinta", Vec4{0.12f, 0.22f, 0.66f, 1.0f});
        p.add_float("ink", "Tinta", 60.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("fill_delay", "Atraso do preenchimento", 35.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.add_int("seed", "Semente", 0, 0, 9999, kAnim);
        p.set_tip(kFillDelay, "Quanto da linha a caneta anda antes de a área atrás dela terminar de encher.");
    }
    static f32 progress(const EffectEval& e) noexcept {
        return std::clamp(e.b(kAuto) ? e.clipProgress : finite_or(e.f(kProgress), 100.0f) / 100.0f, 0.0f, 1.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < kCount) return true;
        const f32 t = progress(e);
        return e.e(kMode) == 0 ? t >= 1.0f : t <= 0.0f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kProgress] = ParamValue::scalar(55.0f);
        v[kRows] = ParamValue::scalar(4.0f);
        v[kStroke] = ParamValue::scalar(4.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ballpoint_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const f32 angle = finite_or(e.f(kDirection), 0.0f) * kDeg;
        const Vec2 dir{std::cos(angle), std::sin(angle)};
        const Vec2 side{-dir.y, dir.x};
        // Extensão da camada ao longo da direção e na perpendicular (pelos cantos).
        f32 u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
        for (const Vec2 c : {Vec2{box.x, box.y}, Vec2{box.x + box.w, box.y}, Vec2{box.x, box.y + box.h},
                             Vec2{box.x + box.w, box.y + box.h}}) {
            const f32 a = c.x * dir.x + c.y * dir.y, b = c.x * side.x + c.y * side.y;
            u0 = std::min(u0, a); u1 = std::max(u1, a);
            v0 = std::min(v0, b); v1 = std::max(v1, b);
        }
        const f32 rows = std::floor(clampf(e.f(kRows), 1.0f, 400.0f, 6.0f) + 0.5f);
        const f32 rowH = std::max((v1 - v0) / rows, 1.0f);
        const f32 stroke = clampf(e.f(kStroke), 0.5f, 600.0f, 6.0f);
        // Passo do zigue-zague: densidade 100% = um vértice a cada 1,2 traço (a
        // faixa sai quase cheia); 1% = um vértice a cada 1,5 altura de faixa.
        const f32 density = clampf(e.f(kScribble), 1.0f, 100.0f, 50.0f) / 100.0f;
        const f32 loose = (1.0f - density) * (1.0f - density);
        const f32 step = std::max(1.0f, 1.2f * stroke + loose * std::max(1.5f * rowH - 1.2f * stroke, 0.0f));
        Uniforms u{};
        u.p0 = Vec4{progress(e), static_cast<f32>(std::min<u32>(e.e(kMode), 1u)), rows,
                    clampf(e.f(kFillDelay), 0.0f, 200.0f, 35.0f) / 100.0f};
        u.p1 = Vec4{dir.x, dir.y, u0, std::max(u1 - u0, 1.0f)};
        u.p2 = Vec4{v0, std::max(v1 - v0, 1.0f), stroke, step};
        u.p3 = Vec4{clampf(e.f(kJitter), 0.0f, 100.0f, 40.0f) / 100.0f, clampf(e.f(kInk), 0.0f, 100.0f, 60.0f) / 100.0f,
                    static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 9999)), 0.0f};
        u.color = premultiplied_linear(e.color(kInkColor));
        return run_pass(ctx, e, ShaderId::effects_ballpoint_frag, in, in.region, u, "caneta", out);
    }
};

} // namespace

void register_water_wrap_ballpoint_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<WaterReflection>());
    (void)r.add(std::make_unique<LightWrap>());
    (void)r.add(std::make_unique<Ballpoint>());
}

} // namespace aurea::builtin
