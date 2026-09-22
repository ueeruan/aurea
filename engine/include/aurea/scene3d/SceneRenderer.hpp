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
#include "aurea/scene3d/SceneAsset.hpp"

#include <future>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aurea::scene3d {

struct GpuPrimitive {
    u32  firstIndex = 0;
    u32  indexCount = 0;
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

/// Uma layer de modelo no frame.
struct SceneInstance {
    /// Dono compartilhado: apagar a layer no meio do frame não invalida o
    /// asset que o render ainda está desenhando.
    std::shared_ptr<const SceneAsset> asset;
    u64 assetKey = 0;
    Mat4 world = Mat4::identity();     ///< cena do modelo → mundo (px)
    std::vector<Mat4> nodeWorld;       ///< por nó, no espaço da cena (animação avaliada)
    std::vector<Mat4> jointMatrices;   ///< por skin, achatado (juntas × inversa de bind)
    std::vector<u32>  skinJointOffset; ///< início de cada skin em jointMatrices
    bool castShadows = true;
};

struct SceneCamera {
    Mat4 view = Mat4::identity();      ///< vista ← mundo (x direita, y baixo, z frente)
    Vec3 position{0, 0, 0};
    f32  fovY = 0.785f;                ///< radianos
    f32  nearZ = 1.0f;                 ///< px
};

struct SceneEnvironment {
    f32  intensity = 1.0f;
    f32  exposure = 1.0f;
    f32  rotation = 0.0f;              ///< radianos em torno do eixo vertical
    Vec3 sky{0.80f, 0.85f, 0.95f};
    Vec3 ground{0.30f, 0.28f, 0.26f};
};

/// Tudo que um grupo 3D precisa para um frame. Montado no prepare (com o
/// modelo travado), consumido no render (sem trava).
struct SceneFrame {
    SceneCamera camera;
    SceneEnvironment environment;
    std::vector<SceneLight> lights;
    std::vector<SceneInstance> instances;
};

struct SceneStats {
    u32 drawCalls = 0;
    u32 triangles = 0;
    u32 visiblePrimitives = 0;
    u32 culledPrimitives = 0;
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
                             u64 frameNumber, FGTexture& outColor) noexcept;

    /// Pipelines 3D para aquecer junto com os 2D.
    void collect_pipelines(std::vector<PipelineKey>& out) const;

    /// Troca o ambiente (IBL). Sem chamada, o primeiro grupo 3D usa o estúdio
    /// neutro padrão.
    [[nodiscard]] Status set_environment(const EnvironmentMaps& maps) noexcept;
    [[nodiscard]] bool has_environment() const noexcept { return irradiance_.valid(); }
    /// Export e captura usam a qualidade final: esperam o ambiente (ou o geram
    /// agora). O preview não chama — segue sem travar.
    void finish_environment() noexcept;

    [[nodiscard]] const SceneStats& stats() const noexcept { return stats_; }
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

    GPUBackend* gpu_ = nullptr;
    ShaderLibrary* shaders_ = nullptr;
    std::unordered_map<u64, Entry> models_;
    // Matrizes de junta: um buffer mapeado por chamada de build, num anel
    // (a GPU ainda pode estar lendo os de frames anteriores).
    static constexpr u32 kJointRing = 8;
    BufferHandle jointBuf_[kJointRing]{};
    usize jointCap_[kJointRing]{};
    u32 jointSlot_ = 0;
    TextureHandle white_{}, flatNormal_{}, black_{}, envCube_{}, brdfLut_{};
    TextureHandle irradiance_{}, prefiltered_{}, iblLut_{};
    u32 prefilteredMips_ = 1;
    /// Estúdio padrão sendo gerado numa thread de fundo (~0,3 s no desktop):
    /// o primeiro quadro 3D não espera; usa o céu analítico até ficar pronto.
    std::future<EnvironmentMaps> pendingEnv_;
    bool envRequested_ = false;
    SamplerHandle cubeSampler_{};
    void release_environment() noexcept;
    SceneStats stats_{};
};

} // namespace aurea::scene3d
