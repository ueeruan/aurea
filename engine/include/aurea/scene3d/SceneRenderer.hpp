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

#include <algorithm>
#include <future>
#include <memory>
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
    f32  intensity = 1.0f;            ///< já em unidades do mundo do Aurea
    f32  range = 0.0f;
    f32  innerCone = 0.0f, outerCone = 0.7853982f;
    /// Só a primeira DIRECIONAL com sombra projeta (mapa ortográfico ajustado
    /// aos modelos do grupo).
    bool castShadows = false;
};

/// O ambiente (luz de imagem) de uma cena — ou de UM objeto, quando ele tem o
/// seu (v22). Nulo = estúdio neutro.
struct SceneEnvironment {
    u64  hdriKey = 0;
    std::shared_ptr<const HdriPixels> hdri;
    f32  intensity = 1.0f;
    f32  exposure = 1.0f;
    f32  rotation = 0.0f;              ///< radianos em torno do eixo vertical
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
    Mat4 view = Mat4::identity();      ///< vista ← mundo (x direita, y baixo, z frente)
    Vec3 position{0, 0, 0};
    f32  fovY = 0.785f;                ///< radianos
    f32  nearZ = 1.0f;                 ///< px
};

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

struct SceneFrame {
    SceneCamera camera;
    SceneEnvironment environment;
    std::vector<SceneLight> lights;
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
    static constexpr u32 kJointRingDecl = 8;
public:
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
    [[nodiscard]] bool build(FrameGraph& graph, Arena& arena, const SceneFrame& frame, u32 width, u32 height,
                             u64 frameNumber, FGTexture& outColor, const std::vector<ScenePlane>* planes = nullptr,
                             const SceneParticleDraw* particles = nullptr, u32 particleCount = 0) noexcept;
    [[nodiscard]] PipelineKey plane_key() const noexcept;

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

    [[nodiscard]] const SceneStats& stats() const noexcept { return stats_; }
    /// Qualidade do preview (HeavyQuality): mapa de sombra (512..2048), filtro
    /// (2 = PCF 6×6, 1 = 2×2 bilinear, 0 = uma amostra) e viés do LOD. O
    /// export chama com (2048, 2, 1).
    /// Instancing ligado (padrão). Desligar serve só ao benchmark A/B.
    void set_instancing(bool on) noexcept { instancing_ = on; }
    void set_quality(u32 shadowMapSize, u32 shadowFilter, f32 lodBias, bool lodHysteresis = true) noexcept {
        lodHysteresis_ = lodHysteresis;
        shadowSize_ = std::clamp(shadowMapSize, 256u, 4096u);
        shadowFilter_ = std::min(shadowFilter, 2u);
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
    /// Instâncias por quadro (instancing): mat4 do mundo + matriz de normais
    /// (PBR) e luz ← local (sombra), no anel dos buffers de junta.
    BufferHandle instBuf_[kJointRingDecl]{};
    usize instCap_[kJointRingDecl]{};
    u32 instSlot_ = 0;

    GPUBackend* gpu_ = nullptr;
    ShaderLibrary* shaders_ = nullptr;
    std::unordered_map<u64, Entry> models_;
    // Matrizes de junta: um buffer mapeado por chamada de build, num anel
    // (a GPU ainda pode estar lendo os de frames anteriores).
    static constexpr u32 kJointRing = kJointRingDecl;
    BufferHandle jointBuf_[kJointRing]{};
    usize jointCap_[kJointRing]{};
    u32 jointSlot_ = 0;
    // Morph: vértices deformados na CPU por quadro (posição + shading), no
    // mesmo esquema de anel.
    BufferHandle morphBuf_[kJointRing]{};
    usize morphCap_[kJointRing]{};
    u32 morphSlot_ = 0;
    TextureHandle white_{}, flatNormal_{}, black_{}, envCube_{}, brdfLut_{};
    TextureHandle irradiance_{}, prefiltered_{}, iblLut_{};
    u32 prefilteredMips_ = 1;
    /// Estúdio padrão sendo gerado numa thread de fundo (~0,3 s no desktop):
    /// o primeiro quadro 3D não espera; usa o céu analítico até ficar pronto.
    std::future<EnvironmentMaps> pendingEnv_;
    bool envRequested_ = false;
    u64 envKey_ = ~0ull;       ///< o que está na GPU
    u64 pendingKey_ = ~0ull;   ///< o que está sendo gerado
    /// Pede o ambiente do quadro: gera fora da thread de render e troca quando
    /// ficar pronto (o anterior continua valendo até lá).
    void request_environment(const SceneEnvironment& env) noexcept;
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
