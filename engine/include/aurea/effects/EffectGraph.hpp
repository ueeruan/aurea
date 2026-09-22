// =============================================================================
//  Aurea / effects / EffectGraph.hpp
//
//  AureaEffectGraph — a pilha de efeitos de UMA layer, transformada em nós do
//  FrameGraph.
//
//      Video Layer → Exposure → Blur → Glow → Motion Tile → saída
//
//  vira, depois do planejamento:
//
//      [cor-fundida(Exposure)] → [blur: redução, H, V] → [glow: brilho,
//       redução, H, V, soma] → [motion-tile] → composição
//
//  O planejamento (`plan`) é separado da montagem (`build`) de propósito:
//
//   - `plan` roda sob o lock do modelo: resolve parâmetros no instante, tira os
//     efeitos neutros, agrupa os por pixel consecutivos (PASS FUSION), decide
//     se o Transform final entra na matriz da composição (sem textura) e soma
//     as margens de vizinhança. Tudo o que depende da timeline fica copiado no
//     plano.
//   - `build` roda sem o lock: só lê o plano e declara passes.
//
//  Regra de honestidade: quando fundir não é possível (curvas demais no mesmo
//  passe, efeito de domínio no meio), o plano registra POR QUÊ em `blockers` —
//  o painel DEV mostra, em vez de fingir que fundiu.
// =============================================================================
#pragma once

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/timeline/Layer.hpp"

#include <utility>
#include <vector>

namespace aurea {

/// Uma etapa do plano.
struct EffectStage {
    enum class Kind : u8 {
        FusedColor = 0,   ///< N efeitos por pixel num passe
        Single,           ///< um efeito com passes próprios
    };
    Kind kind = Kind::FusedColor;
    u32  begin = 0;       ///< faixa em `EffectPlan::evals`
    u32  count = 0;
    f32  margin = 0.0f;   ///< vizinhança que as etapas SEGUINTES leem
};

/// Máximo de operações num passe de cor fundido (casa com MAX_OPS do shader).
inline constexpr u32 kMaxFusedColorOps = 12;

struct EffectPlan {
    LayerPlacement           placement{};
    std::vector<EffectEval>  evals;       ///< só os vivos, em ordem
    std::vector<ParamValue>  values;      ///< armazenamento de `EffectEval::values`
    std::vector<ColorOp>     colorOps;    ///< um por eval PerPixel (mesmo índice)
    std::vector<EffectStage> stages;

    /// Transform final dobrado na composição.
    bool hasFold = false;
    Mat4 foldMatrix = Mat4::identity();
    f32  foldOpacity = 1.0f;

    u32 droppedIdentity = 0;   ///< efeitos neutros removidos
    u32 droppedUnknown = 0;    ///< tipo que esta versão não conhece
    u32 fusedEffects = 0;      ///< efeitos que entraram num passe fundido
    std::vector<std::pair<u32, const char*>> blockers;

    void clear() noexcept;
    [[nodiscard]] bool empty() const noexcept { return stages.empty(); }
};

class EffectGraph {
public:
    /// Resolve e planeja. `resources` pode ser nulo (testes sem GPU): aí
    /// efeitos que precisam de LUT ficam sem ela e o passe fundido a ignora.
    static void plan(const Layer& layer, const EffectRegistry& registry, FrameIndex localTime,
                     f32 texelScale, const LayerPlacement& placement,
                     EffectResources* resources, EffectPlan& out);

    /// Declara os passes do plano no FrameGraph e devolve a imagem final da
    /// layer. Sem etapas, devolve a própria entrada.
    [[nodiscard]] static Status build(const EffectPlan& plan, EffectBuildContext& ctx,
                                      const LayerImage& input, LayerImage& out);

    /// Etapas de efeito que falharam ao montar e viraram bypass (§117): a
    /// camada segue com a imagem de antes do efeito, o quadro sai, e isto sobe.
    /// O log sai uma vez por tipo de efeito, não a cada quadro.
    [[nodiscard]] static u64 bypassed_total() noexcept;
};

} // namespace aurea
