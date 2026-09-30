// =============================================================================
//  Aurea / scene3d / SceneRenderer.hpp
//
//  O 3D dentro do renderer do Aurea — não um motor à parte.
//
//  O Renderer agrupa layers 3D consecutivas da pilha num "grupo 3D"; o grupo
//  vira passes no MESMO FrameGraph (profundidade + PBR, depois sombra/IBL
//  quando entrarem) e sai numa textura linear pré-multiplicada do tamanho da
//  composição, que o compositor desenha na posição do grupo na pilha. Layers
//  2D entre duas layers 3D quebram o grupo: a ordem da pilha continua valendo
//  (regra da composição, ver SCENE3D.md).
//
//  Recursos de GPU por ASSET (GpuModel), compartilhados por todas as layers
//  que usam o mesmo modelo: duas layers do mesmo GLB = uma cópia na GPU.
// =============================================================================
#pragma once

#include "aurea/memory/Arena.hpp"
#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/scene3d/Environment.hpp"

#include <utility>
#include <vector>
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace aurea::scene3d {

struct GpuPrimitive {
    u32  firstIndex = 0;
    u32  indexCount = 0;
    /// Níveis de detalhe (0 = o próprio firstIndex/indexCount).
    static constexpr u32 kMaxLods = 3;
    u32  lodFirst[kMaxLods]{};
    u32  lodCount[kMaxLods]{};
    u32  lodLevels = 1;
    i32  vertexOffset = 0;
    u32  vertexCount = 0;
    i32  material = -1;
    bool skinned = false;
    Aabb bounds{};
};

struct GpuMaterial {
    Material factors;                  ///< cópia dos fatores (o asset é imutável)
    TextureHandle tex[5]{};            ///< base, mr, normal, oclusão, emissiva
    SamplerHandle samp[5]{};
};

/// Os buffers e texturas de UM asset na GPU.
class GpuModel {
public:
    [[nodiscard]] Status upload(GPUBackend& gpu, const SceneAsset& asset) noexcept;
    void release(GPUBackend& gpu) noexcept;

    BufferHandle positions{}, shading{}, skin{}, indices{};
    IndexType indexType = IndexType::U32;
    std::vector<std::vector<GpuPrimitive>> meshes;   ///< por malha do asset
    std::vector<GpuMaterial> materials;
    GpuMaterial defaultMaterial;
    u64 geometryBytes = 0;
    u64 textureBytes = 0;

private:
    std::vector<TextureHandle> ownedTextures_;
    std::vector<SamplerHandle> ownedSamplers_;
};

enum class LightKindGpu : u8 { Directional = 0, Point, Spot };

struct SceneLight {
    LightKindGpu kind = LightKindGpu::Directional;
    Vec3 position{0, 0, 0};           ///< mundo (px)
    Vec3 direction{0, 1, 0};          ///< para onde a luz aponta (mundo)
    Vec3 color{1, 1, 1};
    /// Já em unidades do mundo do Aurea (px). Direcional: irradiância.
    /// Ponto/spot: candela × (px/m)² — o Renderer converte a intensidade da
    /// luz (candela, KHR_lights_punctual) com a escala da composição
    /// (`scene_pixels_per_meter`), e a queda 1/d² do shader em px dá o mesmo
    /// que 1/d² em metros.
    f32  intensity = 1.0f;
    f32  range = 0.0f;
    f32  innerCone = 0.0f, outerCone = 0.7853982f;
    /// Só a primeira DIRECIONAL com sombra projeta (mapa ortográfico ajustado
    /// aos modelos do grupo).
    bool castShadows = false;
    /// Viés constante da sombra (LightData::shadowBias): 0,001 = meio texel do
    /// mapa em profundidade de mundo (o normal offset faz o grosso).
    f32  shadowBias = 0.001f;
    /// Tamanho da fonte (tangente do raio angular): a penumbra do PCSS tem
    /// largura ≈ 2·tan·(distância bloqueador → receptor). 0,04 ≈ 2,3° — uma
    /// caixa de luz de estúdio; o sol real é ~0,0047.
    f32  sourceRadius = 0.04f;
};

/// Sombra da composição (ShadowSettings): o nível de qualidade (HeavyQuality)
/// escolhe mapa e amostras; isto só SOBE o piso ou ajusta o viés — o padrão
/// de um projeto antigo (sem nada escolhido) não perde a sombra suave.
struct SceneShadowSettings {
    f32  normalBias = 0.02f;   ///< normal offset: 0,02 = 1,5 texel do mapa (escala linear)
    bool softShadows = false;  ///< true = PCSS até no BAIXO/MÉDIO
    u32  pcfSamples = 0;       ///< piso de amostras do PCF (0 = o do nível)
    u32  mapResolution = 0;    ///< piso do lado do mapa (0 = o do nível)
};

/// Escala física da cena 3D: a altura do quadro da composição vale 2 m no
/// plano Z = 0 (a câmera padrão mostra esse plano 1:1 em pixels). Assim uma
/// luz pontual de 8 cd a ~1 altura de distância dá ~1,9 de irradiância — a
/// mesma ordem da direcional padrão (3) — em qualquer resolução da composição.
[[nodiscard]] inline f32 scene_pixels_per_meter(u32 compHeight) noexcept {
    return std::max(1.0f, static_cast<f32>(compHeight) * 0.5f);
}

/// O ambiente (luz de imagem) de uma cena — ou de UM objeto, quando ele tem o
/// seu (v22). Nulo = estúdio neutro.
struct SceneEnvironment {
    bool showBackground = false;
    u64  hdriKey = 0;
    std::shared_ptr<const HdriPixels> hdri;
    f32  intensity = 1.0f;
    f32  exposure = 1.0f;
    f32  rotation = 0.0f;              ///< radianos em torno do eixo vertical
    /// Desfoque do fundo visível, 0..1 (EnvironmentSettings::backgroundBlur,
    /// lido como rugosidade: 0 = nítido; forte = o especular pré-filtrado).
    f32  backgroundBlur = 0.0f;
    Vec3 sky{0.80f, 0.85f, 0.95f};
    Vec3 ground{0.30f, 0.28f, 0.26f};
};

struct SceneInstance {
    /// Dono compartilhado: apagar a layer no meio do frame não invalida o
    /// asset que o render ainda está desenhando.
    std::shared_ptr<const SceneAsset> asset;
    u64 assetKey = 0;
    Mat4 world = Mat4::identity();     ///< cena do modelo → mundo (px)
    std::vector<Mat4> nodeWorld;       ///< por nó, no espaço da cena (animação avaliada)
    std::vector<Mat4> jointMatrices;   ///< por skin, achatado (juntas × inversa de bind)
    std::vector<u32>  skinJointOffset; ///< início de cada skin em jointMatrices
    std::vector<std::vector<f32>> morphWeights;   ///< por nó (vazio = pesos da malha)
    std::vector<MaterialOverride> materials; ///< Per-instance factors; GPU/source assets remain shared.
    /// Opacidade por nó (vazio = tudo 1): letras do texto 3D animadas. Abaixo
    /// de 1 o nó mistura como transparente; perto de 0 nem desenha.
    std::vector<f32> nodeOpacity;
    bool castShadows = true;
    u64  layerKey = 0;                 ///< camada de origem (sub-quadros do desfoque)
    bool motionBlur = false;           ///< a camada pede desfoque de movimento
    /// Ambiente PRÓPRIO deste objeto (v22). `ownEnvironment` falso = usa o do
    /// grupo. O estado é do objeto; os mapas na GPU são compartilhados por
    /// asset (a chave é o HDRI), então dois objetos com o mesmo HDRI custam um
    /// upload só.
    bool ownEnvironment = false;
    SceneEnvironment environment;
};

struct SceneCamera {
    Mat4 imageTransform = Mat4::identity(); ///< clip-to-clip framing of a tracked source
    Mat4 view = Mat4::identity();      ///< vista ← mundo (x direita, y baixo, z frente)
    Vec3 position{0, 0, 0};
    f32  fovY = 0.785f;                ///< radianos
    f32  nearZ = 1.0f;                 ///< px
    // --- Lente: profundidade de campo (CameraData, no instante) --------------
    bool dof = false;                  ///< falso = imagem toda nítida (câmera padrão/editor)
    f32  focusDistance = 0.0f;         ///< px do mundo, ao longo do eixo ótico
    f32  fStop = 2.8f;
    f32  blurAmount = 1.0f;            ///< × o círculo de confusão físico
    f32  pixelsPerMeter = 1.0f;        ///< scene_pixels_per_meter(altura da composição)
};

/// Profundidade de campo pela LENTE FINA. Diâmetro do círculo de confusão no
/// sensor de um ponto a S2 com foco em S1 (tudo em mm):
///     c = f² / (N·(S1 − f)) · |S2 − S1| / S2
/// f = a focal equivalente do FOV vertical no sensor de 24 mm (12/tan(fovY/2),
/// vale também com FOV animado), N = f-stop; distâncias do mundo em px viram
/// mm pela escala física da cena. No quadro: c / 24 mm × altura (px); o raio
/// é a metade. Como S2 = nearZ / d (profundidade Z reversa de far infinito),
///     raio(d) = cocScale · (1 − focusOverNear · d)
/// — linear em d: negativo = antes do foco (perto), positivo = depois.
struct DofLens {
    f32 cocScale = 0.0f;       ///< px (raio no infinito, já × blurAmount)
    f32 focusOverNear = 0.0f;  ///< S1 / nearZ
    f32 maxRadius = 0.0f;      ///< teto do raio (px): o desfoque é limitado
    [[nodiscard]] bool active() const noexcept { return cocScale > 0.05f && maxRadius >= 0.5f; }
    /// Raio (px, com sinal) de um ponto a `depth` px do mundo no eixo ótico.
    [[nodiscard]] f32 radius_at_depth(f32 depth, f32 nearZ) const noexcept {
        const f32 d = nearZ / std::max(depth, 1e-3f);
        return std::clamp(cocScale * (1.0f - focusOverNear * d), -maxRadius, maxRadius);
    }
};
[[nodiscard]] DofLens dof_lens(const SceneCamera& camera, u32 imageHeight) noexcept;

/// Tudo que um grupo 3D precisa para um frame. Montado no prepare (com o
/// modelo travado), consumido no render (sem trava).
/// Camada 2D no espaço 3D desenhada dentro da cena (profundidade de verdade
/// com os modelos e as outras camadas 3D). A textura é a imagem final da
/// camada (efeitos aplicados); clipFromLayer = projeção × mundo da camada.
struct ScenePlane {
    FGTexture texture{};
    u64  sampler = 0;
    Mat4 clipFromLayer = Mat4::identity();
    Vec4 region{};                 ///< px da camada
    f32  opacity = 1.0f;
    f32  viewDepth = 0.0f;         ///< w do centro (ordem do mais longe para o mais perto)
};

/// Pós do grupo 3D vindo da composição (PostProcessSettings): o que a pessoa
/// escolheu. A resolução do AA/bloom vem da qualidade (HeavyQuality + nível).
struct ScenePost {
    u32  toneMapper = 0;        ///< 0 PBR Neutral, 1 AgX
    bool bloom = true;
    f32  bloomIntensity = 0.6f; ///< 0..4
    f32  bloomThreshold = 0.0f; ///< 0 = sem limiar (mistura conservadora)
    u32  quality = 0;           ///< Scene3DQuality
};

/// Chão do grupo 3D (FloorSettings da composição; ver Ground.cpp). Plano
/// horizontal no ponto mais baixo dos modelos ("para cima" = −Y).
struct SceneFloor {
    u32  mode = 0;                    ///< 0 sem chão, 1 visível, 2 só sombra/reflexo (shadow catcher)
    Vec3 color{0.5f, 0.5f, 0.5f};     ///< linear
    f32  roughness = 0.35f;
    f32  reflectivity = 0.5f;         ///< 0 = sem passe de reflexo planar
    f32  contactShadow = 0.8f;
    f32  fade = 6.0f;                 ///< raio do desbotamento (× raio dos modelos)
};

struct SceneFrame {
    SceneCamera camera;
    SceneEnvironment environment;
    ScenePost post;
    SceneFloor floor;
    std::vector<SceneLight> lights;
    SceneShadowSettings shadow;
    std::vector<SceneInstance> instances;
    /// Desfoque de movimento: a cena em K instantes do obturador (câmera,
    /// modelos e pose da animação). Vazio = sem desfoque; o render acumula a
    /// média dos K quadros.
    std::vector<SceneFrame> blurFrames;
    /// Índices (no snapshot) das camadas 2D que vivem nesta cena.
    std::vector<u32> planeLayers;
    /// Índices (no snapshot) das camadas de partículas desenhadas DENTRO desta
    /// cena (billboards com teste de profundidade). Só no quadro base; os
    /// subquadros do desfoque usam a lista dele.
    std::vector<u32> particleLayers;
    /// Subquadro do desfoque: deslocamento em quadros do instante do quadro
    /// (0 no quadro base).
    f64 subFrame = 0.0;
};

/// Desenho instanciado translúcido no passe da cena (partículas 3D, 8.2):
/// testa a profundidade de modelos e planos e NÃO escreve nela — partícula
/// aditiva não depende de ordem; a normal é desenhada na ordem das instâncias
/// (sem ordenar por profundidade: aproximação documentada em ParticleScene).
struct SceneParticleDraw {
    PipelineHandle pipeline{};
    const void* uniforms = nullptr;   ///< memória do quadro (arena do Renderer)
    u32 uniformBytes = 0;
    u8  push[128]{};
    u32 pushBytes = 0;
    BufferHandle history{};           ///< binding 16 (AUREA_DATA1)
    BufferHandle quad{};              ///< 6 índices u16
    u32 instances = 0;
    // Aurea Particular 8.2 (render/ParticleExtras): dados extras no binding 15
    // (AUREA_DATA), imagem da partícula de textura e a malha instanciada
    // (`meshVertices` > 0: desenho não indexado, vértice = vértice da malha).
    BufferHandle extras{};
    TextureHandle texture{};
    u64 sampler = 0;
    u32 meshVertices = 0;
    /// Chave do pipeline: o build troca amostras (MSAA) e o segundo alvo (MRT)
    /// para os do passe da cena — quem monta a partícula não precisa saber.
    PipelineKey key{};
};

/// Contas do QUADRO inteiro (todas as cenas e subquadros de desfoque): o
/// build zera ao ver um `frameNumber` novo e soma dentro do mesmo quadro.
struct SceneStats {
    u32 drawCalls = 0;           ///< draw_indexed da cor (depois do instancing)
    u32 shadowDrawCalls = 0;     ///< draw_indexed do mapa de sombra
    u32 instancedDraws = 0;      ///< desenhos que juntaram ≥ 2 instâncias
    u32 triangles = 0;
    u32 visiblePrimitives = 0;   ///< primitivas × instâncias que passaram no frustum
    u32 culledPrimitives = 0;    ///< fora do frustum (não desenhadas)
    u32 shadowMapSize = 0;       ///< lado do mapa de sombra usado (0 = sem sombra)
    u64 geometryBytes = 0;
    u64 textureBytes = 0;
};

/// Matriz clip ← vista, perspectiva com Z REVERSO e far infinito: perto → 1,
/// infinito → 0. Precisão de profundidade uniforme em cena grande, e sem
/// plano distante cortando o cenário.
[[nodiscard]] Mat4 reverse_z_perspective(f32 fovY, f32 aspect, f32 nearZ) noexcept;

/// Câmera padrão da composição (quando não há layer de câmera): no eixo
/// central, a uma distância em que o plano Z=0 mapeia 1:1 em pixels — uma
/// layer 3D sem rotação fica no MESMO lugar em que estaria como 2D.
[[nodiscard]] SceneCamera default_camera(u32 compWidth, u32 compHeight) noexcept;

class SceneRenderer {
public:
    /// Buffers de upload por build (juntas, morph, instâncias). Cada build pega
    /// um livre, e ele só volta à fila quando a GPU conclui o quadro que o leu
    /// (`defer_until_gpu_done`). Era um anel fixo de 8: o desfoque de movimento
    /// grava até 64 cenas por quadro, e a 9ª sobrescrevia as poses que a 1ª
    /// ainda ia ler — export com desfoque errado e diferente a cada execução.
    struct UploadPool {
        struct Buf { BufferHandle handle{}; usize cap = 0; };
        BufferUsage usage = BufferUsage::Storage;
        usize minBytes = 0;
        const char* name = "";
        std::mutex mutex;                  ///< a devolução pode vir de outra thread (Metal)
        std::vector<Buf> free;
        std::vector<BufferHandle> all;     ///< vivos (livres ou em voo), para o shutdown
        u32 generation = 0;                ///< muda no shutdown/perda do device: devolução velha é descartada
    };

    [[nodiscard]] Status initialize(GPUBackend& gpu, ShaderLibrary& shaders) noexcept;
    void shutdown() noexcept;
    void forget_device() noexcept;

    /// GPU do asset, criada na primeira vez (upload síncrono nesta fase).
    [[nodiscard]] const GpuModel* model(u64 assetKey, const SceneAsset& asset) noexcept;
    /// Solta modelos que nenhum frame usou nos últimos `idleFrames`.
    void collect(u64 frameNumber, u64 idleFrames = 240) noexcept;
    void release_all() noexcept;

    /// Monta os passes do grupo: cor (RGBA16F, limpa transparente) e
    /// profundidade (transitória). `outColor` recebe a textura a compor.
    ///
    /// O passe da cena desenha em DOIS alvos (MRT): 0 = 2D de exibição
    /// (planos, partículas, unlit — sai exatamente como no 2D), 1 = a cena em
    /// luz linear HDR. Com MSAA os dois são multiamostrados e transitórios, e
    /// o resolve do fim do passe (no tile) entrega os de 1 amostra. Depois,
    /// o pós do grupo: exposição → bloom → tone map → + 2D → (FXAA). Grupo
    /// só com planos/partículas não tem HDR: um alvo só, sem pós.
    ///
    /// `outDepth` (opcional): profundidade de 1 amostra para quem lê depois
    /// (SSAO/sombra de contato). Com MSAA ela sai do resolve da amostra 0 —
    /// só onde o aparelho tem (`depthResolveSampleZero`); sem, fica inválida
    /// e quem precisa faz um pré-passe de profundidade de 1 amostra.
    [[nodiscard]] bool build(FrameGraph& graph, Arena& arena, const SceneFrame& frame, u32 width, u32 height,
                             u64 frameNumber, FGTexture& outColor, const std::vector<ScenePlane>* planes = nullptr,
                             const SceneParticleDraw* particles = nullptr, u32 particleCount = 0,
                             FGTexture* outDepth = nullptr) noexcept;
    /// O grupo tem luz de cena (modelo, texto 3D, céu)? Sem ela, planos e
    /// partículas vão num alvo só, sem pós.
    [[nodiscard]] static bool wants_hdr(const SceneFrame& frame) noexcept {
        return !frame.instances.empty() || frame.environment.showBackground;
    }
    /// AA e pós do quadro (HeavyQuality já com o nível da composição):
    /// amostras pedidas (o aparelho corta), FXAA, bloom (resolução inicial
    /// 1/div e níveis).
    void set_post_quality(u32 msaaSamples, bool fxaa, u32 bloomStartDiv, u32 bloomLevels) noexcept {
        postMsaa_ = std::clamp(msaaSamples, 1u, 8u);
        postFxaa_ = fxaa;
        bloomDiv_ = std::clamp(bloomStartDiv, 1u, 4u);
        bloomLevels_ = std::clamp(bloomLevels, 1u, 8u);
    }
    /// Amostras que o passe da cena usa de fato (pedido ∩ aparelho).
    [[nodiscard]] u32 pass_samples() const noexcept;
    /// Profundidade de campo: amostras do disco por pixel (o preview usa
    /// poucas, o export muitas). 0 = desligada mesmo com a lente pedindo.
    void set_dof_quality(u32 taps) noexcept { dofTaps_ = std::min(taps, 256u); }
    [[nodiscard]] PipelineKey plane_key(bool translucent = false) const noexcept;

    /// Pipelines 3D para aquecer junto com os 2D.
    void collect_pipelines(std::vector<PipelineKey>& out) const;

        /// Conjunto de mapas JÁ subidos, por chave de HDRI — o cache que faz dois
    /// objetos com o mesmo ambiente custarem um upload só.
    struct EnvSet {
        TextureHandle irradiance{}, prefiltered{}, brdf{};
        u32 mips = 0;
        u64 lastFrame = 0;
    };
    /// Troca o ambiente (IBL). Sem chamada, o primeiro grupo 3D usa o estúdio
    /// neutro padrão.
    [[nodiscard]] Status set_environment(const EnvironmentMaps& maps) noexcept;
    [[nodiscard]] Status upload_environment(const EnvironmentMaps& maps, EnvSet& out) noexcept;
    /// Devolve (subindo se preciso) o conjunto daquele ambiente. Nulo = sem
    /// como (usa o do grupo).
    [[nodiscard]] const EnvSet* environment_set(const SceneEnvironment& env, u64 frameNumber) noexcept;
    [[nodiscard]] bool has_environment() const noexcept { return irradiance_.valid(); }
    [[nodiscard]] bool environment_pending() const noexcept { return pendingEnv_.valid(); }
    /// Export e captura usam a qualidade final: esperam o ambiente (ou o geram
    /// agora). O preview não chama — segue sem travar.
    void finish_environment(const SceneEnvironment& env) noexcept;
    /// Ambiente atual (0 = estúdio; ~0 = nenhum ainda).
    [[nodiscard]] u64 environment_key() const noexcept { return envKey_; }
    /// Pede o ambiente do quadro: gera fora da thread de render e troca quando
    /// ficar pronto (o anterior continua valendo até lá). Pedir de novo o que
    /// já está na GPU não gera nada.
    void request_environment(const SceneEnvironment& env) noexcept;
    /// Quantas vezes um ambiente do grupo subiu para a GPU (desde o início).
    [[nodiscard]] u64 environment_uploads() const noexcept { return envUploads_; }
    /// Qualidade do IBL: o preview gera `preview` (fora da thread de render,
    /// troca quando pronto); export/captura (`finish_environment`) esperam o
    /// `final`. O que já está na GPU nunca é rebaixado (o final serve ao
    /// preview). Padrão: especular 256²/fundo 512² e 512²/1024².
    void set_environment_quality(const EnvironmentQuality& preview, const EnvironmentQuality& final) noexcept {
        envPreview_ = preview;
        envFinal_ = final;
    }
    /// Tamanhos do que está na GPU (0 = nada): base do especular e face do fundo.
    [[nodiscard]] u32 environment_specular_size() const noexcept { return prefilteredSize_; }
    [[nodiscard]] u32 environment_background_size() const noexcept { return backgroundSize_; }

    [[nodiscard]] const SceneStats& stats() const noexcept { return stats_; }
    /// Qualidade (HeavyQuality): mapa de sombra (256..4096), nível da sombra
    /// (0 BAIXO = PCF 8, 1 MÉDIO = PCF 16, 2 ALTO = PCSS 12+24, 3 ULTRA =
    /// PCSS 16+32) e viés do LOD. O export chama com (4096, 3, 1).
    /// Instancing ligado (padrão). Desligar serve só ao benchmark A/B.
    void set_instancing(bool on) noexcept { instancing_ = on; }
    /// AA do 3D ligado (padrão). Desligar (1 amostra, sem FXAA) serve só ao
    /// A/B de teste e benchmark — a referência "sem AA" das medições.
    void set_antialias(bool on) noexcept { antialias_ = on; }
    void set_quality(u32 shadowMapSize, u32 shadowFilter, f32 lodBias, bool lodHysteresis = true) noexcept {
        lodHysteresis_ = lodHysteresis;
        shadowSize_ = std::clamp(shadowMapSize, 256u, 4096u);
        shadowFilter_ = std::min(shadowFilter, 3u);
        lodBias_ = std::clamp(lodBias, 0.1f, 1.0f);
    }
    [[nodiscard]] u64 resident_bytes() const noexcept;

private:
    struct Entry {
        std::unique_ptr<GpuModel> model;
        u64 lastFrame = 0;
        bool failed = false;
    };

    [[nodiscard]] PipelineKey key_for(AlphaMode mode, bool doubleSided, bool skinned) const noexcept;
    /// Estado do passe da cena no build corrente (as chaves dependem dele).
    u32  passSamples_ = 1;
    bool passMrt_ = false;
    bool passA2C_ = false;
    u32  postMsaa_ = 4;
    bool postFxaa_ = false;
    bool antialias_ = true;
    u32  bloomDiv_ = 2;
    u32  bloomLevels_ = 6;
    /// Pós do grupo: bloom (cadeia descida/subida), tone map e FXAA. Devolve
    /// a textura final do grupo.
    [[nodiscard]] FGTexture build_post(FrameGraph& graph, const SceneFrame& frame, u32 width, u32 height,
                                       FGTexture display, FGTexture scene) noexcept;
    [[nodiscard]] FGTexture build_fxaa(FrameGraph& graph, u32 width, u32 height, FGTexture src) noexcept;
    /// Profundidade de campo ANTES do bloom e do tone map (luz HDR linear: o
    /// realce vira bokeh). Dois passes: raio do círculo de confusão por pixel
    /// (da profundidade de 1 amostra) e o gather no disco, que devolve cena e
    /// 2D de exibição desfocados (MRT). Falso = segue sem DOF.
    [[nodiscard]] bool build_dof(FrameGraph& graph, const SceneFrame& frame, u32 width, u32 height,
                                 FGTexture& display, FGTexture& scene, FGTexture depth) noexcept;
    u32 dofTaps_ = 32;
    [[nodiscard]] PipelineKey shadow_key(bool skinned) const noexcept;
    u32 shadowSize_ = 2048;
    u32 shadowFilter_ = 2;
    f32 lodBias_ = 1.0f;
    bool lodHysteresis_ = true;
    bool instancing_ = true;
    u64 statsFrame_ = ~0ull;
    /// Nível de LOD da última escolha por (camada, nó, primitiva): a troca só
    /// acontece fora de uma faixa de ±15% em volta do limiar (sem "piscar"
    /// quando o tamanho na tela oscila em cima dele).
    struct LodState { u8 level = 0; u64 lastFrame = 0; };
    std::unordered_map<u64, LodState> lodState_;

    GPUBackend* gpu_ = nullptr;
    ShaderLibrary* shaders_ = nullptr;
    std::unordered_map<u64, Entry> models_;
    // Um buffer mapeado por chamada de build: matrizes de junta, vértices de
    // morph deformados na CPU (posição + shading) e instâncias (mat4 do mundo
    // + normais para o PBR e luz ← local para a sombra).
    std::shared_ptr<UploadPool> jointPool_ = make_upload_pool(BufferUsage::Storage, 64 * sizeof(Mat4), "3d-juntas");
    std::shared_ptr<UploadPool> morphPool_ = make_upload_pool(BufferUsage::Vertex, 0, "3d-morph");
    std::shared_ptr<UploadPool> instPool_ = make_upload_pool(BufferUsage::Storage, 64 * sizeof(Mat4), "3d-instancias");
    static std::shared_ptr<UploadPool> make_upload_pool(BufferUsage usage, usize minBytes, const char* name);
    /// Buffer de upload mapeável com ≥ `bytes`, devolvido ao pool quando a GPU
    /// concluir o quadro em gravação. Inválido = sem memória de GPU.
    [[nodiscard]] BufferHandle take_upload(const std::shared_ptr<UploadPool>& pool, usize bytes) noexcept;
    /// Destrói (ou, com o device perdido, só esquece) os buffers dos pools.
    void drop_upload_pools(bool destroy) noexcept;
    TextureHandle white_{}, flatNormal_{}, black_{}, envCube_{}, brdfLut_{};
    TextureHandle irradiance_{}, prefiltered_{}, iblLut_{}, background_{};
    u32 prefilteredMips_ = 1;
    u32 prefilteredSize_ = 0, backgroundSize_ = 0;
    EnvironmentQuality envPreview_ = EnvironmentQuality::preview();
    EnvironmentQuality envFinal_ = EnvironmentQuality::final_quality();
    /// Qualidade PEDIDA do que está na GPU e do que está sendo gerado (tetos
    /// do especular e do fundo; 0 = sem fundo). Decide se um pedido já está
    /// atendido sem depender do tamanho real (um HDRI pequeno gera menos).
    u32 envSpecTier_ = 0, envBgTier_ = 0, pendingSpecTier_ = 0, pendingBgTier_ = 0;
    /// Estúdio padrão sendo gerado numa thread de fundo (~0,3 s no desktop):
    /// o primeiro quadro 3D não espera; usa o céu analítico até ficar pronto.
    std::future<EnvironmentMaps> pendingEnv_;
    bool envRequested_ = false;
    u64 envKey_ = ~0ull;       ///< o que está na GPU
    u64 pendingKey_ = ~0ull;   ///< o que está sendo gerado
    u64 envUploads_ = 0;
    SamplerHandle cubeSampler_{};
    void release_environment() noexcept;
    /// Conjuntos por HDRI: um upload por asset, compartilhado pelos objetos.
    /// Teto pequeno — cada conjunto é um cubemap com mips.
    static constexpr usize kMaxEnvSets = 4;
    std::vector<std::pair<u64, EnvSet>> envSets_;
    usize envSetFrame_ = 0;
    SceneStats stats_{};
};

} // namespace aurea::scene3d
