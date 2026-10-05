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

#include <span>
#include <memory>
#include <limits>
#include <cmath>

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
    Fill = 11,       ///< tinge a camada com uma cor chapada (Preencher)
    BalanceHls = 12, ///< matiz/luz/saturação no HSL (Equilíbrio de cor)
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
    /// A later tiling stage can read any part of this image, including pixels
    /// outside the final viewport. Earlier effects must retain their bounds.
    bool preserveFullExtent = false;
    /// Lens flare is generated in camera pixels at the projected light origin.
    bool sceneFlare = false;
    Vec2 flarePosition{};
    /// Camada do Particular (ParticleEmitter::Particular): a folha é desenhada
    /// SEM a rotação da camada e as partículas vivem no espaço 3D dela.
    /// `worldFromLayer` leva px da camada (z para longe) ao mundo, com a
    /// rotação X/Y/Z, a orientação, a posição Z e os pais; `compFromWorld` é a
    /// câmera da composição (a ativa, ou a padrão de 40°) até px da composição
    /// (homogêneo); `camRight`/`camUp` são os eixos da câmera no mundo — os
    /// sprites ficam de frente para ela.
    bool particleSpace = false;
    Mat4 worldFromLayer = Mat4::identity();
    Mat4 compFromWorld = Mat4::identity();
    Mat4 previousParticleProjection = Mat4::identity(); ///< layer -> camera pixels one frame earlier
    Vec3 camRight{1.0f, 0.0f, 0.0f};
    Vec3 camUp{0.0f, 1.0f, 0.0f};
    /// Obturador (graus) do desfoque de movimento da camada: > 0 só com a
    /// chave da camada e o desfoque da composição ligados. O Particular usa
    /// para o rastro por partícula (a folha em si não se move).
    f32 shutterAngle = 0.0f;
    f32 shutterPhase = 0.0f;
};

/// Retângulo, em pixels da layer, que o quadro inteiro da composição cobre
/// (caixa da inversa dos quatro cantos). Vazio se a matriz é degenerada.
[[nodiscard]] Rect visible_layer_rect(const LayerPlacement& placement) noexcept;

/// Um valor de preset: parâmetro (índice da declaração) e valor parado
/// (componente 0; bool = 0/1, enum = índice).
struct EffectPresetValue {
    u32 param;
    f32 value;
};
/// Preset de um efeito (Effect::presets). `id` é estável — a interface traduz
/// o nome por ele; `name` é o texto do motor (pt-BR).
struct EffectPreset {
    const char* id;
    const char* name;
    std::span<const EffectPresetValue> values;
};

/// Uma imagem no plano da layer: a textura e o retângulo (px da layer, em
/// resolução cheia) que ela cobre. Com efeito que expande (blur sem repetir
/// borda, Motion Tile), a região passa da caixa da layer.
struct LayerImage {
    FGTexture texture{};
    Rect      region{};
    u32       width = 0;    ///< texels
    u32       height = 0;
    /// A FONTE da camada num instante anterior (Detectar movimento), na mesma
    /// região e densidade da entrada. Só a imagem que sai da fonte traz isto;
    /// o EffectGraph passa pelas etapas anteriores ao efeito que pede e a
    /// entrega pelo contexto (`EffectBuildContext::history`).
    FGTexture history{};

    [[nodiscard]] bool valid() const noexcept { return texture.valid() && width && height; }
    /// Texels por pixel de layer.
    [[nodiscard]] f32 texel_scale_x() const noexcept { return region.w > 0 ? width / region.w : 1.0f; }
    [[nodiscard]] f32 texel_scale_y() const noexcept { return region.h > 0 ? height / region.h : 1.0f; }
};

class Effect;
struct Layer;

/// De qual camada o Espectro de áudio lê o som (ver `EffectResources::audio_spectrum`).
enum class AudioSpectrumSource : u8 {
    /// A própria camada se ela tem som; senão a camada com som mais próxima
    /// ABAIXO dela na pilha; senão a primeira da composição que tem som.
    Automatic = 0,
    /// Só a própria camada (um vídeo com trilha). Sem som, o espectro repousa.
    ThisLayer,
    /// A primeira camada com som da composição (a de baixo).
    FirstWithAudio,
};

/// O que o Espectro de áudio pede ao renderer no planejamento.
struct AudioSpectrumRequest {
    const Layer* host = nullptr;       ///< a camada dona do efeito (só no planejamento)
    AudioSpectrumSource source = AudioSpectrumSource::Automatic;
    u32 bands = 32;                    ///< 1..kAudioSpectrumMaxBands
    f32 gain = 1.0f;                   ///< sensibilidade (1 = 0 dB)
};

/// Teto de faixas de um espectro (uma textura `bands`×1).
inline constexpr u32 kAudioSpectrumMaxBands = 128;

/// O que o Mapa de profundidade (IA) pede ao renderer no planejamento.
struct DepthMapRequest {
    /// A camada dona (só no planejamento). Nula = a prévia do catálogo, que
    /// usa a foto das prévias recortada como a cartela.
    const Layer* host = nullptr;
    const EffectInstance* instance = nullptr; ///< a instância (estado da suavização)
    FrameIndex localTime{0};
    /// Suavização no tempo dos limites (percentis 2º/98º): 0 = cada quadro com
    /// os seus; perto de 1 = os limites andam devagar e o mapa não "respira".
    f32 smoothing = 0.7f;
    bool foreground = false;
};

/// A disparidade 256×256 da FONTE da camada (R: normalizada pelos percentis
/// do próprio quadro, maior = mais perto) e os limites suavizados no MESMO
/// espaço: o shader prende em [lo, hi].
struct DepthMapResult {
    TextureHandle texture{};
    f32 lo = 0.0f;
    f32 hi = 1.0f;
    bool failed = false;
    i64 sourceTimeUs = -1; ///< foreground preview must pair this source frame with its mask
};

/// O que a Forma de onda e o Espectro de áudio pedem ao renderer: o som de
/// UMA camada (com os efeitos de áudio dela) numa janela em volta do quadro.
struct AudioAnalysisRequest {
    const Layer* host = nullptr;          ///< a camada dona (só no planejamento)
    const EffectInstance* instance = nullptr;
    u64 layer = 0;                        ///< a camada escolhida (LayerId empacotado); 0 = a própria
    bool spectrum = false;                ///< false = forma de onda
    u32 count = 0;                        ///< amostras exibidas / faixas
    f32 durationMs = 200.0f;
    f32 offsetMs = 0.0f;
    u32 channel = 0;                      ///< forma de onda: 0 mono, 1 esquerdo, 2 direito
    f32 startHz = 20.0f, endHz = 1000.0f; ///< espectro
    bool averaging = false;               ///< espectro: média de janelas ao longo da duração
};

/// Textura `count`×1 (forma de onda: R = amostra com sinal; espectro: R =
/// magnitude linear) e a faixa mais forte (0..1 do caminho).
struct AudioAnalysisResult {
    TextureHandle texture{};
    f32 peak = 0.0f;
};

/// Recursos persistentes de efeito (LUTs de curva). Implementado pelo renderer.
class EffectResources {
public:
    virtual ~EffectResources() = default;
    // Equal-distance positions (xy) and tangent (z, radians), in host-layer pixels.
    [[nodiscard]] virtual std::vector<Vec4> repeat_path(const Layer*, u32, f32) noexcept { return {}; }
    /// LUT 256x1 da curva, criada/atualizada só quando a curva muda.
    [[nodiscard]] virtual TextureHandle curve_lut(const CurveData& curve) noexcept = 0;
    [[nodiscard]] virtual TextureHandle cube_lut(AssetId) noexcept { return {}; }
    /// Fração das amostras que os efeitos caros usam neste quadro (0,25..1).
    /// Preview adaptativo/calor < 1; export e prévia do catálogo = 1 sempre.
    [[nodiscard]] virtual f32 effect_quality() const noexcept { return 1.0f; }
    /// Espectro do som que uma camada toca NESTE quadro: textura `bands`×1
    /// (R = magnitude 0..1 da faixa, dos graves aos agudos; G = nível geral
    /// do quadro). Calculado do áudio decodificado, sem estado entre quadros:
    /// o mesmo quadro dá a mesma textura no preview, no scrubbing e no export.
    /// Nula = sem som, sem decoder ou sem GPU (o efeito desenha o repouso).
    [[nodiscard]] virtual TextureHandle audio_spectrum(const AudioSpectrumRequest& request) noexcept {
        (void)request;
        return TextureHandle{};
    }
    /// Mapa de profundidade da fonte da camada neste quadro (ai/DepthMapService:
    /// um cálculo por quadro-fonte, o mesmo no preview e no export). Textura
    /// nula = camada sem foto/vídeo, rede indisponível ou, no preview de um
    /// vídeo, ainda calculando — o efeito devolve a entrada.
    [[nodiscard]] virtual DepthMapResult depth_map(const DepthMapRequest& request) noexcept {
        (void)request;
        return DepthMapResult{};
    }
    /// O som da camada escolhida no quadro do planejamento, analisado (ver
    /// `AudioAnalysisRequest`). Textura nula = sem som / sem GPU: o efeito
    /// desenha o repouso (linha reta).
    [[nodiscard]] virtual AudioAnalysisResult audio_analysis(const AudioAnalysisRequest& request) noexcept {
        (void)request;
        return AudioAnalysisResult{};
    }
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
    f64                   framesPerSecond = 30.0;
    /// Texels por pixel de layer na resolução de trabalho. Todo comprimento
    /// em pixel (raio de blur, passo de nitidez) é multiplicado por isto.
    f32                   texelScale = 1.0f;
    const LayerPlacement* placement = nullptr;
    /// A camada dona do efeito. Como `instance`, só vale no PLANEJAMENTO.
    const Layer*          layer = nullptr;
    /// Recurso resolvido no planejamento por `Effect::resolve_resources` (o
    /// espectro do som, por exemplo) e que VIAJA até a montagem — como a LUT
    /// da curva no ColorOp. Textura persistente do renderer, fora do grafo.
    TextureHandle         aux{};
    Vec4                  auxInfo{};   ///< o que o efeito quiser anotar junto (nº de faixas...)
    i64                   foregroundSourceTimeUs = -1;
    std::shared_ptr<const std::vector<Vec4>> pathSamples;
    f64 fractionalTime = std::numeric_limits<f64>::quiet_NaN();

    [[nodiscard]] f64 time_frames() const noexcept {
        return std::isfinite(fractionalTime) ? fractionalTime : static_cast<f64>(localTime.value);
    }
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

    /// Passe de GEOMETRIA gerada no vértice (sem vertex buffer): `vertexCount`
    /// vértices pelo pipeline (vertex, fragment), cor limpa para transparente
    /// e, com `depth`, teste/escrita de profundidade (Z reverso, textura
    /// transitória do tamanho do alvo). É o das bolas: uma instância por
    /// célula da grade, desenhada como um quadrado que o fragmento arredonda.
    /// Com `blend`, o pipeline mistura pré-multiplicado no alvo (`BlendMode::Add`
    /// = soma; qualquer outro = "sobre"), para geometria que se sobrepõe sem
    /// profundidade (as partículas do Particular).
    u32 geometry_pass(const char* name, PassStage stage, FGTexture target, ShaderId vertex, ShaderId fragment,
                      std::initializer_list<PassTexture> textures, const void* uniforms, u32 uniformBytes,
                      u32 vertexCount, bool depth, bool blend = false,
                      BlendMode blendMode = BlendMode::Normal) noexcept;

    /// Resolução de uma região em texels na escala pedida, limitada ao máximo
    /// do aparelho (a escala é reduzida uniformemente se passar).
    void region_size(const Rect& region, f32 texelScale, u32& outW, u32& outH) const noexcept;

    /// `uvMap` que leva o uv (0..1) da SAÍDA para o uv da ENTRADA quando as
    /// regiões diferem: uv_in = uv_out * xy + zw.
    [[nodiscard]] static Vec4 uv_map(const Rect& outRegion, const Rect& inRegion) noexcept;

    /// A camada num instante anterior, já pelas etapas que vêm antes do efeito
    /// (só durante o `build` de um efeito com `Effect::wants_history`). Inválida
    /// quando a fonte não tem passado (imagem, texto, forma: parados no tempo).
    [[nodiscard]] const LayerImage& history() const noexcept { return history_; }
    void set_history(const LayerImage& image) noexcept { history_ = image; }

    /// Outra camada da composição desenhada como ENTRADA de um efeito (o mapa
    /// do Mapa de deslocamento): a imagem dela no quadro da composição
    /// (região 0,0..largura,altura da composição, em px da composição), com
    /// transform, máscaras e efeitos dela, no MESMO instante. O renderer
    /// desenha as camadas pedidas por `Effect::input_layer_param` antes das
    /// outras e as publica aqui.
    struct LayerInput {
        u64 layer = 0;          ///< LayerId empacotado (com o sal da pré-composição)
        LayerImage image{};
    };
    void set_layer_inputs(std::span<const LayerInput> inputs) noexcept { layerInputs_ = inputs; }
    /// A camada `layer` (LayerId empacotado, como no parâmetro de referência)
    /// desenhada neste quadro; nula se ela não aparece agora (fora do tempo,
    /// apagada, sem GPU). Dentro de uma pré-composição os ids do renderer
    /// levam o sal dela: a comparação usa índice baixo + geração.
    [[nodiscard]] const LayerImage* layer_input(u64 layer) const noexcept;

private:
    FrameGraph&      graph_;
    ShaderLibrary&   shaders_;
    Arena&           arena_;
    EffectResources& resources_;
    SurfaceFormat    workFormat_;
    u32              maxTexture_;
    LayerImage       history_{};
    std::span<const LayerInput> layerInputs_{};
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

    /// Presets do efeito (o do app antigo: Impacto/Na mão/Glitch do Tremor
    /// em trancos). Cada um escreve os valores parados dos parâmetros que
    /// lista (Engine::apply_effect_preset, um passo de desfazer); a interface
    /// mostra uma fileira de fichas no topo do cartão. Vazio = sem fileira.
    [[nodiscard]] virtual std::span<const EffectPreset> presets() const noexcept { return {}; }

    /// Só efeitos por pixel: a operação que entra no passe de cor fundido.
    [[nodiscard]] virtual bool color_op(const EffectEval& eval, ColorOp& out) const noexcept {
        (void)eval; (void)out;
        return false;
    }

    /// Recursos que só existem sob o lock do modelo (o espectro do som da
    /// camada, por exemplo) são resolvidos AQUI, no planejamento, e guardados
    /// em `eval.aux`/`eval.auxInfo` para a montagem. `eval.resources` pode
    /// ser nulo (teste sem GPU): aí o efeito monta sem o recurso.
    virtual void resolve_resources(EffectEval& eval) const noexcept { (void)eval; }

    /// O efeito compara a camada com ela mesma num instante ANTERIOR (Detectar
    /// movimento): o renderer decodifica a fonte nesse instante e o EffectGraph
    /// a leva pelas etapas anteriores até ele (`EffectBuildContext::history`).
    [[nodiscard]] virtual bool wants_history() const noexcept { return false; }

    /// Índice do parâmetro (referência a camada) cuja camada o efeito LÊ como
    /// imagem (Mapa de deslocamento); −1 = nenhum. O renderer inclui essa
    /// camada no quadro mesmo com o olho desligado, desenha-a na composição
    /// e a entrega por `EffectBuildContext::layer_input`.
    [[nodiscard]] virtual i32 input_layer_param() const noexcept { return -1; }

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
