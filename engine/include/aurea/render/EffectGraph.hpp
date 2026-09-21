// =============================================================================
//  Aurea / render / EffectGraph.hpp
//
//  A cadeia de efeitos de uma layer, compilada para o MENOR número de passes.
//
//  O problema que isto resolve, em números: uma layer com correção de cor
//  (exposição, contraste, saturação, temperatura), matriz de cor, opacidade e
//  vinheta é, ingenuamente, 6 passes sobre 4K. Seis leituras e seis escritas de
//  ~33 MB (RGBA16F) dão ~400 MB de tráfego de memória POR FRAME. A 60 fps isso
//  é 24 GB/s só de banda — mais do que a memória de um celular entrega.
//
//  A saída não é "otimizar o shader". É NÃO PASSAR PELA MEMÓRIA:
//
//     Effect Fusion  — efeitos que são função de um pixel (cor, opacidade,
//                      vinheta, curva) viram UMA expressão no mesmo shader.
//                      O compilador de shader funde as expressões e o custo
//                      vira uma leitura e uma escrita, não seis.
//
//     Pass Fusion    — efeitos que precisam de vizinhança (blur, glow, dilate)
//                      não podem fundir com cor, mas fundem ENTRE SI quando
//                      compartilham o mesmo kernel e o mesmo raio.
//
//     Shader Fusion  — blurs de raios diferentes (glow = blur grande + blur
//                      pequeno) usam um passe de downsample compartilhado em
//                      vez de dois passes completos.
//
//  O resultado prático daquele exemplo: 2 passes. Um até o blur, um depois.
//
//  Regra de honestidade: quando a fusão NÃO é matematicamente possível, o grafo
//  diz que não é e emite os passes separados. Nunca finge que fundiu e produz
//  imagem diferente do export.
// =============================================================================
#pragma once

#include "aurea/render/GPUBackend.hpp"
#include "aurea/render/FrameGraph.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/core/Result.hpp"

#include <vector>
#include <string>

namespace aurea {

/// Classificação de um efeito para fins de fusão. É o que decide o que pode
/// entrar no mesmo shader.
enum class EffectClass : u8 {
    /// Função pura do pixel de entrada: `out = f(in, params)`.
    /// Ex.: exposição, contraste, saturação, opacidade, vinheta, matriz de cor,
    /// curva, LUT, blend de cor, preto-e-branco.
    /// → FUNDE SEMPRE. Um shader, N passes.
    PerPixel = 0,

    /// Precisa dos pixels vizinhos num raio fixo.
    /// Ex.: blur gaussiano, glow, sharpen, unsharp mask, dilate/erode.
    /// → Funde com outros do mesmo kernel/raio em duas passadas (H + V).
    Neighborhood,

    /// Precisa de mais de uma amostra temporal (frames diferentes).
    /// Ex.: eco, motion blur acumulativo, RGB time warp, trail, ghosting.
    /// → Não funde. Precisa de um recurso persistente, e o grafo declara isso.
    Temporal,

    /// Precisa do frame inteiro (redução, histograma, sort de pixel).
    /// Ex.: auto-levels, pixel sort, equalização.
    /// → Não funde. Um passe de compute dedicado.
    Global,

    /// Muda a geometria ou o domínio da imagem.
    /// Ex.: displacement, motion tile, distortion, warp, corner pin.
    /// → Não funde com PerPixel depois dele (o domínio muda).
    Domain,

    /// Produz uma máscara para outro efeito consumir.
    /// Ex.: extração de luminância, chroma key como gerador de matte.
    /// → Não funde. Vira uma entrada do efeito consumidor.
    MatteGenerator,
};

/// Uma etapa já resolvida da cadeia: ou um passe único, ou um grupo fundido.
struct CompiledEffectStage {
    /// Nome para o painel de debug: "cor-fundida(4)", "blur-h", "blur-v".
    std::string name;

    /// Índices dos efeitos desta layer que entraram nesta etapa, na ordem em
    /// que foram aplicados. Vazio para etapas internas de blur (H/V).
    std::vector<u32> effectIndices;

    EffectClass cls = EffectClass::PerPixel;
    PassStage   stage = PassStage::Effects;

    /// Quantas amostras de textura esta etapa faz por pixel. É a métrica de
    /// custo real: uma leitura = 1, um gaussiano de 9 taps = 9, um blur de 32
    /// samples = 32.
    u32 sampleCount = 1;

    /// Precisa de um render target intermediário com formato de maior precisão?
    /// (blur acumulando em 8 bits perde qualidade visível em HDR)
    bool requiresFloatTarget = false;

    /// Precisa de um recurso persistente entre frames (histórico)?
    bool requiresHistory = false;

    /// Resolução relativa ao alvo. 0.5 = meia resolução, que é o truque padrão
    /// para blur grande ficar barato sem perder qualidade visível.
    f32 resolutionScale = 1.0f;

    /// Reduzido no preview? Efeitos caros têm versão mais barata. `previewSamples`
    /// é o que o preview usa; o export SEMPRE usa `sampleCount`.
    u32 previewSampleCount = 0;   ///< 0 = igual ao de export

    /// Custo estimado relativo (pixels × amostras). Base do preview adaptativo.
    u64 estimatedCost = 0;
};

/// Plano de render dos efeitos de uma layer.
struct EffectPlan {
    /// Etapas a executar, em ordem.
    std::vector<CompiledEffectStage> stages;

    /// Efeitos desabilitados ou que não contribuem (parâmetro neutro: blur com
    /// raio 0, opacidade 1 sobre entrada opaca). Removidos da cadeia — não
    /// custam nada.
    std::vector<u32> droppedEffects;

    /// Efeitos que exigem um passe dedicado porque a fusão não é possível.
    /// Registrado explicitamente para o painel de debug poder explicar por quê.
    std::vector<std::pair<u32, const char*>> fusionBlockers;

    /// Número de passes de GPU que este plano gera.
    [[nodiscard]] u32 pass_count() const noexcept { return static_cast<u32>(stages.size()); }

    /// Soma dos custos. O scheduler compara com o orçamento de frame.
    [[nodiscard]] u64 total_cost() const noexcept {
        u64 c = 0;
        for (const auto& s : stages) c += s.estimatedCost;
        return c;
    }
};

/// Registro de efeitos disponíveis. Estático, preenchido uma vez.
class EffectRegistry {
public:
    static constexpr u32 kMaxEffects = 256;

    /// Registra um efeito. `description` precisa ter vida maior que o registro
    /// (é string literal, na prática).
    [[nodiscard]] Status register_effect(const EffectDesc* description,
                                         EffectClass cls, u32 typeId) noexcept;

    [[nodiscard]] const EffectDesc* description(u32 typeId) const noexcept;
    [[nodiscard]] EffectClass classification(u32 typeId) const noexcept;
    [[nodiscard]] u32 count() const noexcept { return count_; }

    /// Índice pelo nome, para importar projetos e para a UI montar o menu.
    [[nodiscard]] u32 find(const char* name) const noexcept;

    /// Lista de nomes para a UI, agrupada por categoria. A UI itera isto e
    /// monta o painel de "adicionar efeito" sem conhecer nenhum efeito.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (u32 i = 0; i < count_; ++i) fn(i, *descriptions_[i], classes_[i]);
    }

private:
    const EffectDesc* descriptions_[kMaxEffects]{};
    EffectClass       classes_[kMaxEffects]{};
    u32               count_ = 0;
};

/// Compilador da cadeia de efeitos.
///
/// Sem estado de propósito. O plano compilado vai para o `EffectPlan` de quem
/// chamou, então duas layers podem ser compiladas em paralelo sem disputar nada
/// — o que o export paralelo precisa.
class EffectCompiler {
public:
    /// Compila a lista de efeitos de uma layer num plano de passes.
    ///
    /// `targetWidth/Height` são a resolução em que a layer será composta — o
    /// custo estimado depende dela, e é o que permite ao scheduler decidir
    /// reduzir resolução antes de reduzir um efeito.
    ///
    /// `preview` seleciona os parâmetros de preview (menos amostras). O export
    /// chama com `preview = false` e recebe a versão final — é assim que
    /// preview e export ficam visualmente iguais, porque é a MESMA função
    /// gerando os dois planos.
    [[nodiscard]] static Status compile(const std::vector<Effect>& effects,
                                        const TrackSet& tracks, FrameIndex time,
                                        const EffectRegistry& registry,
                                        u32 targetWidth, u32 targetHeight,
                                        bool preview,
                                        EffectPlan& out) noexcept;

    /// Decide se um efeito é neutro no frame atual e pode ser removido.
    /// Ex.: blur com raio 0, opacidade 100%, correção com todos os valores no
    /// padrão. Remover é sempre melhor que aplicar um passe que não muda nada.
    ///
    /// `effectIndex` é a posição do efeito no vetor da layer — é essa a chave
    /// usada nos tracks, não o `Effect::id` (que existe para a UI). Misturar os
    /// dois faria a animação de um efeito alimentar outro.
    [[nodiscard]] static bool is_neutral(const Effect& effect, u32 effectIndex,
                                          const EffectDesc& desc,
                                          const TrackSet& tracks, FrameIndex time) noexcept;
};

} // namespace aurea
