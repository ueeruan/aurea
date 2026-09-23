// =============================================================================
//  Aurea / effects / Effect.hpp
//
//  A API de efeito do Aurea.
//
//  Um efeito é um objeto SEM ESTADO (um por tipo, no registro). O estado de
//  cada instância — os valores dos parâmetros — mora na layer. A cada frame, o
//  EffectGraph resolve os valores no instante (`EffectEval`) e pergunta ao
//  efeito uma de duas coisas:
//
//   - efeito POR PIXEL: "qual é a tua operação de cor?" (`color_op`). O
//     EffectGraph junta as operações consecutivas num único passe
//     (`color_stack.frag`). O efeito não escreve passe nenhum.
//
//   - qualquer outro: "monte os teus passes" (`build`). O efeito declara
//     texturas e passes no FrameGraph pelo `EffectBuildContext`; o grafo decide
//     ordem, memória e barreiras.
//
//  O efeito NÃO sabe se o frame é preview ou export, nem em que resolução está:
//  ele recebe `texelScale` (texels por pixel da layer) e converte o que é
//  comprimento em pixel. Preview em 1/4 e export em 4K saem do mesmo código.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/memory/Arena.hpp"
#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/ShaderLibrary.hpp"

#include <initializer_list>
#include <vector>

namespace aurea {

enum class EffectClass : u8 {
    /// Função do pixel: `out = f(in)`. Funde com os vizinhos da mesma classe.
    PerPixel = 0,
    /// Precisa da vizinhança (blur, glow, sharpen). Pode expandir a região.
    Neighborhood,
    /// Muda a geometria/domínio (Motion Tile, Transform). Quebra a fusão.
    Domain,
    /// Precisa de outro instante no tempo (eco, trail). Recurso persistente.
    Temporal,
    /// Precisa do frame inteiro (histograma, auto-levels). Compute dedicado.
    Global,
};

struct EffectInfo {
    const char* key = "";        ///< chave estável: "aurea.blur.gaussian"
    const char* name = "";       ///< nome exibido
    const char* category = "";
    EffectClass cls = EffectClass::PerPixel;
};

/// Opcodes do `color_stack.frag`. Os números são contrato com o shader.
enum class ColorOpCode : u32 {
    None = 0,
    Exposure = 1,
    BrightnessContrast = 2,
    Saturation = 3,
    Tint = 4,
    ColorMatrix = 5,
    Levels = 6,
    Curves = 7,
    LumaKey = 8,     ///< mexe no alfa (Chave de luma)
    ChromaKey = 9,   ///< mexe no alfa e tira o derramamento (Chave de croma)
    Invert = 10,     ///< negativo do valor codificado (Inverter)
};

/// Uma operação de cor: 16 floats (4 vec4 no shader). `p[0]` é reservado para
/// o opcode; os demais são do efeito.
struct ColorOp {
    ColorOpCode code = ColorOpCode::None;
    f32 p[16]{};
    /// LUT 256x1 (só `Curves`), resolvida no planejamento. É um handle, não um
    /// ponteiro para a curva: a montagem dos passes roda sem o lock do modelo.
    TextureHandle lut{};
};

/// Onde a layer cai na composição. Efeitos de domínio usam isto para saber
/// quanto do plano da layer aparece no quadro (Motion Tile cobre a composição
/// depois da escala e da rotação da layer).
struct LayerPlacement {
    Mat4 compFromLayer = Mat4::identity();   ///< px da layer → px da composição
    u32  compWidth = 0;
    u32  compHeight = 0;
    u32  layerWidth = 0;    ///< tamanho natural da layer (resolução cheia)
    u32  layerHeight = 0;
    /// A camada é desenhada DENTRO da cena 3D (um plano no mundo), não na
    /// composição. Aí a matriz `compFromLayer` não diz onde ela aparece: o
    /// corte pela área visível da composição não vale, e usá-lo encolhia a
    /// região que o efeito devolve — o brilho (que depende dela) saía 1x1 e a
    /// camada 3D sumia.
    bool inScene3d = false;
};

/// Retângulo, em pixels da layer, que o quadro inteiro da composição cobre
/// (caixa da inversa dos quatro cantos). Vazio se a matriz é degenerada.
[[nodiscard]] Rect visible_layer_rect(const LayerPlacement& placement) noexcept;

/// Uma imagem no plano da layer: a textura e o retângulo (px da layer, em
/// resolução cheia) que ela cobre. Com efeito que expande (blur sem repetir
/// borda, Motion Tile), a região passa da caixa da layer.
struct LayerImage {
    FGTexture texture{};
    Rect      region{};
    u32       width = 0;    ///< texels
    u32       height = 0;

    [[nodiscard]] bool valid() const noexcept { return texture.valid() && width && height; }
    /// Texels por pixel de layer.
    [[nodiscard]] f32 texel_scale_x() const noexcept { return region.w > 0 ? width / region.w : 1.0f; }
    [[nodiscard]] f32 texel_scale_y() const noexcept { return region.h > 0 ? height / region.h : 1.0f; }
};

class Effect;

/// Recursos persistentes de efeito (LUTs de curva). Implementado pelo renderer.
class EffectResources {
public:
    virtual ~EffectResources() = default;
    /// LUT 256x1 da curva, criada/atualizada só quando a curva muda.
    [[nodiscard]] virtual TextureHandle curve_lut(const CurveData& curve) noexcept = 0;
    /// Fração das amostras que os efeitos caros usam neste quadro (0,25..1).
    /// Preview adaptativo/calor < 1; export e prévia do catálogo = 1 sempre.
    [[nodiscard]] virtual f32 effect_quality() const noexcept { return 1.0f; }
};

/// Valores resolvidos de UMA instância num instante.
///
/// `instance` e `resources` só valem durante o PLANEJAMENTO (sob o lock do
/// modelo). Na montagem dos passes o efeito lê apenas `values`, que são cópias.
struct EffectEval {
    const Effect*         effect = nullptr;
    const EffectInstance* instance = nullptr;
    EffectResources*      resources = nullptr;
    const ParamValue*     values = nullptr;
    u32                   valueOffset = 0;     ///< posição em EffectPlan::values
    u32                   count = 0;
    u32                   effectIndex = 0;     ///< posição na layer (painel)
    FrameIndex            localTime{0};
    /// Texels por pixel de layer na resolução de trabalho. Todo comprimento
    /// em pixel (raio de blur, passo de nitidez) é multiplicado por isto.
    f32                   texelScale = 1.0f;
    const LayerPlacement* placement = nullptr;

    [[nodiscard]] const ParamValue& value(u32 i) const noexcept { return values[i]; }
    [[nodiscard]] f32  f(u32 i) const noexcept { return values[i].v[0]; }
    [[nodiscard]] bool b(u32 i) const noexcept { return values[i].as_bool(); }
    [[nodiscard]] u32  e(u32 i) const noexcept { return values[i].as_enum(); }
    [[nodiscard]] Vec2 p2(u32 i) const noexcept { return values[i].as_vec2(); }
    [[nodiscard]] Vec4 color(u32 i) const noexcept { return values[i].as_color(); }
    [[nodiscard]] const CurveData* curve(u32 i) const noexcept {
        const u64 idx = values[i].ref;
        return instance && idx < instance->curves.size() ? &instance->curves[idx] : nullptr;
    }
};

/// Uma textura amarrada a um slot num passe de tela cheia.
struct PassTexture {
    FGTexture     graph{};     ///< textura do grafo (barreira automática)
    TextureHandle raw{};       ///< textura persistente fora do grafo (LUT)
    CommonSampler sampler = CommonSampler::LinearClamp;
};

/// O que o efeito recebe para montar os passes.
class EffectBuildContext {
public:
    EffectBuildContext(FrameGraph& graph, ShaderLibrary& shaders, Arena& frameArena,
                       EffectResources& resources, SurfaceFormat workFormat,
                       u32 maxTextureSize) noexcept
        : graph_(graph), shaders_(shaders), arena_(frameArena), resources_(resources),
          workFormat_(workFormat), maxTexture_(maxTextureSize) {}

    [[nodiscard]] FrameGraph& graph() noexcept { return graph_; }
    [[nodiscard]] ShaderLibrary& shaders() noexcept { return shaders_; }
    [[nodiscard]] EffectResources& resources() noexcept { return resources_; }
    [[nodiscard]] SurfaceFormat work_format() const noexcept { return workFormat_; }
    [[nodiscard]] u32 max_texture_size() const noexcept { return maxTexture_; }

    /// Textura de trabalho (formato de trabalho, alvo de render + amostrável).
    [[nodiscard]] FGTexture texture(const char* name, u32 width, u32 height) noexcept;

    /// Passe de tela cheia: um triângulo, o pipeline pedido, até 4 texturas e
    /// um bloco de uniforms (copiado para a arena do frame).
    u32 fullscreen_pass(const char* name, PassStage stage, FGTexture target, ShaderId fragment,
                        std::initializer_list<PassTexture> textures,
                        const void* uniforms, u32 uniformBytes,
                        LoadOp load = LoadOp::DontCare) noexcept;

    /// Resolução de uma região em texels na escala pedida, limitada ao máximo
    /// do aparelho (a escala é reduzida uniformemente se passar).
    void region_size(const Rect& region, f32 texelScale, u32& outW, u32& outH) const noexcept;

    /// `uvMap` que leva o uv (0..1) da SAÍDA para o uv da ENTRADA quando as
    /// regiões diferem: uv_in = uv_out * xy + zw.
    [[nodiscard]] static Vec4 uv_map(const Rect& outRegion, const Rect& inRegion) noexcept;

private:
    FrameGraph&      graph_;
    ShaderLibrary&   shaders_;
    Arena&           arena_;
    EffectResources& resources_;
    SurfaceFormat    workFormat_;
    u32              maxTexture_;
};

// -----------------------------------------------------------------------------
// O efeito
// -----------------------------------------------------------------------------
class Effect {
public:
    virtual ~Effect() = default;

    [[nodiscard]] virtual const EffectInfo& info() const noexcept = 0;
    [[nodiscard]] EffectTypeId type_id() const noexcept { return effect_type_id(info().key); }
    [[nodiscard]] EffectClass effect_class() const noexcept { return info().cls; }

    virtual void declare_parameters(ParameterRegistry& params) const = 0;

    /// Pipelines que o efeito usa, para o pré-aquecimento.
    virtual void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const {
        (void)out; (void)work;
    }

    /// Nos valores atuais, o efeito não muda nada? (blur de raio 0, exposição
    /// 0). Efeito neutro sai da cadeia — nenhum passe.
    [[nodiscard]] virtual bool is_identity(const EffectEval& eval) const noexcept {
        (void)eval;
        return false;
    }

    /// VALORES DE DEMONSTRAÇÃO — os que a PRÉVIA do catálogo usa (Fase 7.3
    /// §13). O vetor chega preenchido com os padrões da declaração; o efeito
    /// sobrescreve o que precisar para que a prévia MOSTRE o que ele faz.
    ///
    /// Isto existe porque o padrão de fábrica de quase todo efeito é neutro
    /// (desfoque 0, nitidez 0, brilho 0): a prévia de um efeito desligado não
    /// diz nada a ninguém. A instância vem junto porque curvas e degradês
    /// vivem nela, não no valor. Devolver `false` mantém os padrões.
    [[nodiscard]] virtual bool demo_values(EffectInstance& instance,
                                           std::vector<ParamValue>& values) const noexcept {
        (void)instance; (void)values;
        return false;
    }

    /// Só efeitos por pixel: a operação que entra no passe de cor fundido.
    [[nodiscard]] virtual bool color_op(const EffectEval& eval, ColorOp& out) const noexcept {
        (void)eval; (void)out;
        return false;
    }

    /// Margem (px da layer) que o efeito lê em volta de cada pixel. É o que o
    /// EffectGraph soma para recortar a região visível de um efeito anterior
    /// sem cortar o que este precisa.
    [[nodiscard]] virtual f32 input_margin(const EffectEval& eval) const noexcept {
        (void)eval;
        return 0.0f;
    }

    /// Monta os passes. `input` é a imagem de entrada; a saída vai em `out`.
    /// `margin` é quanto da vizinhança além do quadro visível os efeitos
    /// seguintes vão ler (px da layer).
    [[nodiscard]] virtual Status build(EffectBuildContext& ctx, const EffectEval& eval,
                                       const LayerImage& input, f32 margin,
                                       LayerImage& out) const {
        (void)ctx; (void)eval; (void)input; (void)margin; (void)out;
        return Status{Errc::NotImplemented, "efeito sem passe"};
    }

    /// Transform: quando é o ÚLTIMO efeito da layer, ele não precisa de passe —
    /// entra na matriz da composição. Devolve true e ajusta matriz/opacidade.
    [[nodiscard]] virtual bool fold_into_composite(const EffectEval& eval, Mat4& layerMatrix,
                                                   f32& opacity) const noexcept {
        (void)eval; (void)layerMatrix; (void)opacity;
        return false;
    }
};

} // namespace aurea
