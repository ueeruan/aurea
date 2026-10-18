// =============================================================================
//  Progresso do desenho (Desenho) — o desenho aparece traço a traço.
//
//  Pedido da 0.2.5: um efeito que vai DESENHANDO o desenho, acima de tudo o
//  vetorial. O caminho depende da camada:
//
//   - camada VETORIAL: o renderer pergunta `vector_progress` ao montar a malha
//     e corta os contornos pelo comprimento (vector::DrawProgress): geometria
//     exata, a ponta é a tampa do próprio contorno, e compõe com o Aparar de
//     cada grupo (desenha a fração do que o Aparar deixou). Sequencial = um
//     traço depois do outro na ordem do desenho; Simultâneo = todos juntos na
//     mesma fração. O preenchimento entra quando o contorno do grupo fecha
//     (últimos 10 %). Na cadeia de pixels o efeito é neutro: já agiu.
//   - qualquer outra (PNG, vídeo, texto, desenho sem dado vetorial): não há
//     traço para seguir, então ele revela em VARREDURA na direção do Ângulo,
//     com a ponta suave, e a "tinta" (pixel escuro e opaco) chega um pouco
//     antes do papel claro — o nanquim aparece antes da aguada.
//
//  Progresso 100 % = a camada exata (é o padrão: pôr o efeito não muda nada
//  até animar). Inverter começa pelo fim (o último traço primeiro; na
//  varredura, do outro lado). Os índices de parâmetro ficam fixos nos
//  projetos salvos (acrescentar só no fim).
// =============================================================================
#include "CCCommon.hpp"

#include "aurea/timeline/Layer.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace aurea::builtin {
namespace {
using namespace cc;

class DrawingProgress final : public Effect {
public:
    enum : u32 { kProgress = 0, kOrder, kReverse, kSoftness, kAngle };
    enum : u32 { kSequential = 0, kSimultaneous };
    /// Na varredura, o papel claro chega esta fração da varredura depois da
    /// tinta escura (o nanquim antes da aguada).
    static constexpr f32 kInkLead = 0.12f;

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kDrawingProgress, "Progresso do desenho", "Desenho", EffectClass::Neighborhood};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kOrders[] = {"Sequencial", "Simultâneo"};
        p.add_float("progress", "Progresso", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("order", "Ordem", kOrders, 2, kSequential);
        p.add_bool("reverse", "Inverter", false);
        p.add_float("softness", "Suavidade da ponta", 8.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_angle("angle", "Ângulo", 0.0f);
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // A prévia do catálogo: o desenho no meio do caminho, com a ponta suave.
        v[kProgress] = ParamValue::scalar(60.0f);
        v[kSoftness] = ParamValue::scalar(12.0f);
        return true;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_drawing_progress_frag, work));
    }

    /// Fração desenhada, 0..1 (NaN = inteira, o padrão).
    static f32 amount(const EffectEval& e) noexcept { return clampf(e.f(kProgress), 0.0f, 100.0f, 100.0f) / 100.0f; }

    /// A camada tem traço de verdade para seguir (a geometria é cortada no renderer).
    static bool vector_layer(const Layer* l) noexcept {
        return l && !l->adjustment && l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector;
    }

    bool edits_vector_geometry() const noexcept override { return true; }

    bool vector_progress(const EffectEval& e, vector::DrawProgress& out) const noexcept override {
        out.amount = amount(e);
        out.sequential = std::min<u32>(e.e(kOrder), kSimultaneous) == kSequential;
        out.reverse = e.b(kReverse);
        return true;
    }

    // Na camada vetorial o corte já está na malha: nenhum passe. Nas outras,
    // 100 % é a camada exata e sai da cadeia.
    bool is_identity(const EffectEval& e) const noexcept override {
        return vector_layer(e.layer) || !(amount(e) < 1.0f);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const f32 t = amount(e);
        // Depois do Motion Tile a imagem é a PAREDE (continua além da caixa):
        // a varredura atravessa a parede que o quadro vê, não só o ladrilho do
        // centro (como os efeitos que mapeiam a caixa, ver LayerImage::tiled).
        const Rect box = in.tiled ? in.region : layer_box(e, in);
        f32 angle = finite_or(e.f(kAngle), 0.0f) * kDeg;
        if (e.b(kReverse)) angle += kPi;
        const Vec2 dir{std::cos(angle), std::sin(angle)};
        // Comprimento da varredura: a projeção da caixa no eixo (canto a canto).
        const f32 span = std::max(std::fabs(box.w * dir.x) + std::fabs(box.h * dir.y), 1.0f);
        // A ponta nunca fica mais estreita que ~1 texel (borda sem serrilhado).
        const f32 texel = in.texel_scale_x() > 0.0f ? 1.0f / in.texel_scale_x() : 1.0f;
        const f32 soft = std::max(clampf(e.f(kSoftness), 0.0f, 100.0f, 8.0f) / 100.0f, texel / span);
        Uniforms u{};
        // A frente anda de 0 até depois do papel mais atrasado mais a ponta:
        // 0 % não mostra nenhum pixel e 100 % mostra todos.
        u.p0 = Vec4{t * (1.0f + kInkLead + soft), soft, kInkLead, t <= 0.0f ? 1.0f : 0.0f};
        u.p1 = Vec4{box.x + 0.5f * box.w, box.y + 0.5f * box.h, dir.x, dir.y};
        u.p2 = Vec4{span, 0.0f, 0.0f, 0.0f};
        return run_pass(ctx, e, ShaderId::effects_drawing_progress_frag, in, in.region, u, "drawing-progress", out);
    }
};

} // namespace

void register_drawing_progress_effect(EffectRegistry& r) { (void)r.add(std::make_unique<DrawingProgress>()); }

} // namespace aurea::builtin
