// =============================================================================
//  Detectar movimento — mostra ONDE a imagem mudou entre o quadro atual e um
//  quadro anterior da mesma camada (a diferença dos dois), com o resto preto.
//
//  Comportamento (referência de especificação: o S_MotionDetect descrito na
//  documentação da Sapphire; nada de código de terceiros aqui):
//    - Atraso (quadros): de quantos quadros atrás vem a comparação (1..30);
//    - Brilho: multiplica a diferença;
//    - Levantar escuros: soma (ou tira) cinza do resultado — positivo acende
//      as regiões paradas, negativo apaga as diferenças pequenas (ruído);
//    - Saturação: 0 = monocromático, 1 = a cor da diferença, >1 mais cor;
//    - Movimento: Todos (diferença absoluta), Mais claro (só onde clareou),
//      Mais escuro (só onde escureceu);
//    - Mistura: com a imagem original.
//
//  O quadro anterior é a FONTE da camada no instante t − atraso, decodificada
//  pelo renderer (o mesmo cache de quadros do RGB no tempo), e passa pelas
//  mesmas etapas que vêm antes do efeito (EffectGraph::build). Preview, scrub
//  e export leem o mesmo quadro; o export espera o decoder.
//
//  Fonte sem passado (imagem, texto, forma, sólido): parada no tempo, a
//  diferença é zero — o resultado é o "sem movimento" (preto + o cinza de
//  Levantar escuros), como o efeito daria num vídeo parado.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

class MotionDetect final : public Effect {
public:
    enum : u32 { kDelay = 0, kBrightness, kOffsetDarks, kSaturation, kMotion, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kMotionDetect, "Detectar movimento", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("delay_frames", "Atraso (quadros)", 1, 1, kMotionDetectMaxDelay);
        p.add_float("brightness", "Brilho", 1.0f, 0.0f, 10.0f, kParamAnimatable);
        p.typed_range(0.0f, 100.0f);
        p.add_float("offset_darks", "Levantar escuros", 0.0f, -8.0f, 2.0f, kParamAnimatable);
        p.add_float("saturation", "Saturação", 1.0f, 0.0f, 10.0f, kParamAnimatable);
        static const char* const kModes[] = {"Todos", "Mais claro", "Mais escuro"};
        p.add_enum("motion", "Movimento", kModes, 3, 0);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kBrightness] = ParamValue::scalar(4.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_motion_detect_frag, work));
    }
    bool wants_history() const noexcept override { return true; }
    bool is_identity(const EffectEval& e) const noexcept override { return !(e.f(kMix) > 0.01f); }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        if (!input.valid()) return Errc::InvalidArgument;
        const LayerImage& past = ctx.history();
        struct {
            Vec4 uvMap;    // uv da saída → uv do passado
            Vec4 params;   // x brilho, y levantar escuros, z saturação, w modo
            Vec4 extra;    // x mistura, y 1 = há passado
        } u{};
        const bool hasPast = past.valid();
        u.uvMap = hasPast ? EffectBuildContext::uv_map(input.region, past.region) : Vec4{1, 1, 0, 0};
        u.params = Vec4{std::max(0.0f, finite(e.f(kBrightness), 1.0f)),
                        std::clamp(finite(e.f(kOffsetDarks), 0.0f), -8.0f, 2.0f),
                        std::max(0.0f, finite(e.f(kSaturation), 1.0f)),
                        static_cast<f32>(std::min<u32>(e.e(kMotion), 2u))};
        u.extra = Vec4{std::clamp(finite(e.f(kMix), 100.0f) / 100.0f, 0.0f, 1.0f), hasPast ? 1.0f : 0.0f, 0, 0};
        out = LayerImage{ctx.texture("detectar-movimento", input.width, input.height), input.region,
                         input.width, input.height};
        // Sem passado, o slot 1 lê a própria entrada (a diferença dá zero).
        const FGTexture pastTex = hasPast ? past.texture : input.texture;
        if (ctx.fullscreen_pass("detectar-movimento", PassStage::Effects, out.texture, ShaderId::effects_motion_detect_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp},
                                 PassTexture{pastTex, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }

private:
    static f32 finite(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
};

} // namespace

void register_motion_detect_effect(EffectRegistry& r) { (void)r.add(std::make_unique<MotionDetect>()); }

} // namespace aurea::builtin
