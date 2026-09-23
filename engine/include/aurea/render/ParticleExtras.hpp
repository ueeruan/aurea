// =============================================================================
//  Aurea / render / ParticleExtras.hpp  —  AUREA PARTICULAR (8.2)
//
//  Tudo o que o Particular precisa ALÉM do bloco de uniformes (20 vec4, que já
//  bateu no limite da captura do FrameGraph): pontos de emissão de outra
//  camada, a malha da partícula de malha, as curvas ao longo da vida, os
//  aleatórios, a colisão esfera/caixa. Vai num STORAGE BUFFER próprio da
//  camada (binding AUREA_DATA do passe de partículas).
//
//  DUAS PARTES, com ritmos diferentes:
//    - cabeçalho (kHeaderVec4 vec4) — muda a cada quadro (keyframes, matriz da
//      fonte). Anel de kRing cópias no início do buffer, uma por quadro em voo.
//    - estático (pontos + malha) — só muda quando a FONTE muda. Gerado na CPU
//      uma vez por revisão da fonte (cache por chave de conteúdo) e escrito
//      uma vez no buffer da camada.
//
//  Layout do cabeçalho (vec4), espelho de shaders/particles/particles_extras.glsl:
//    H0      nº de pontos, 1º ponto (índice absoluto), modo, tamanho da célula
//            modo: 0 ponto exato, 1 célula (jitter ± célula/2), 2 polilinha
//            (sorteia um segmento e um ponto nele — distribuição por comprimento)
//    H1      nº de vértices da malha, 1º vértice, bandeiras, proporção da textura (l/a)
//    H2      aleatório de tamanho, de opacidade, de cor, probabilidade do aux
//    H3      largura do rastro, opacidade do rastro, escala da malha, malha iluminada
//    H4      colisão: centro (px da camada), raio
//    H5      colisão: meia-caixa (px), elasticidade
//    H6..H9  matriz (colunas): px da camada de partículas ← espaço da fonte
//            (projetiva: a fonte pode estar no 3D — divide por w)
//    H10     nº de paradas de cor, de pontos do tamanho, de pontos da opacidade
//    H11..18 paradas de cor (posição, r, g, b) lineares
//    H19..26 tamanho ao longo da vida (posição, multiplicador)
//    H27..34 opacidade ao longo da vida (posição, multiplicador)
// =============================================================================
#pragma once

#include "aurea/render/GPUBackend.hpp"
#include "aurea/timeline/Layer.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace aurea {

class Composition;
struct ImagePixels;
namespace scene3d { struct SceneAsset; }

namespace particles {

inline constexpr u32 kHeaderVec4 = 36;
/// Cópias do cabeçalho no anel: nenhum quadro em voo lê a cópia que o
/// quadro atual escreve (o mesmo número do anel de glifos/máscaras).
inline constexpr u32 kRing = 4;
/// Teto de pontos de emissão por fonte (64K vec4 = 1 MB).
inline constexpr u32 kMaxEmitPoints = 65536;
/// Teto de triângulos da malha da partícula (instanciada: o custo é
/// triângulos × partículas). Acima disso entra o LOD mais grosso que caiba,
/// e depois uma amostra uniforme dos triângulos.
inline constexpr u32 kMaxMeshTriangles = 12000;

enum HeaderFlag : u32 {
    kFlagSourcePoints = 1u << 0,   ///< o emissor usa os pontos (camada/texto/caminho/malha)
    kFlagTexture      = 1u << 1,   ///< partícula de textura com a imagem pronta
    kFlagMesh         = 1u << 2,   ///< partícula de malha com a malha pronta
};

/// O que só muda quando a fonte muda. Imutável depois de pronto: o snapshot
/// segura um shared_ptr e a GPU copia dele fora do lock.
struct StaticData {
    u64 key = 0;                  ///< conteúdo (fonte + modo); 0 = vazio
    std::vector<Vec4> points;     ///< xyz no espaço da fonte, w livre
    u32 pointMode = 0;            ///< ver H0
    f32 cellSize = 0.0f;
    /// Malha da partícula: 2 vec4 por vértice (posição normalizada — maior
    /// lado = 1, centro na origem, Y para baixo; normal xyz + cor RGBA8 linear
    /// empacotada em w). Triângulos em sequência.
    std::vector<Vec4> mesh;
    [[nodiscard]] u32 mesh_vertices() const noexcept { return static_cast<u32>(mesh.size() / 2); }
    [[nodiscard]] usize bytes() const noexcept { return (points.size() + mesh.size()) * sizeof(Vec4); }
};

/// O pedaço do quadro: cabeçalho + o estático em uso + a textura.
struct FrameData {
    Vec4 header[kHeaderVec4]{};
    std::shared_ptr<const StaticData> statics;
    u64  layerKey = 0;            ///< chave do buffer da camada (id com sal)
    AssetId texture{};            ///< imagem da partícula (Texture), se pronta
    bool meshMode = false;        ///< desenha a malha instanciada
    u32  meshVertices = 0;
};

/// Pontos/malhas em cache por chave de conteúdo. Vive no Renderer; o prepare
/// consulta sob o lock do modelo.
class StaticCache {
public:
    [[nodiscard]] std::shared_ptr<const StaticData> find(u64 key, u64 frameNumber) noexcept;
    void put(std::shared_ptr<const StaticData> data, u64 frameNumber) noexcept;
    /// Solta o que não foi usado nos últimos `age` quadros.
    void collect(u64 frameNumber, u64 age = 240) noexcept;
    void clear() noexcept { entries_.clear(); }
    [[nodiscard]] u64 resident_bytes() const noexcept;
    [[nodiscard]] u32 builds() const noexcept { return builds_; }
    [[nodiscard]] u32 hits() const noexcept { return hits_; }
private:
    struct Entry { std::shared_ptr<const StaticData> data; u64 lastFrame = 0; };
    std::unordered_map<u64, Entry> entries_;
    u32 builds_ = 0, hits_ = 0;
};

/// Quem o prepare precisa para montar os dados (tudo opcional).
struct BuildContext {
    const Composition* comp = nullptr;
    FrameIndex time{0};
    const ImagePixels* (*imageLookup)(void*, AssetId) = nullptr;
    void* imageCtx = nullptr;
    std::shared_ptr<const scene3d::SceneAsset> (*modelLookup)(void*, AssetId) = nullptr;
    void* modelCtx = nullptr;
    u64 frameNumber = 0;
};

/// Monta o cabeçalho do quadro e resolve (ou gera) a parte estática. `pd` já
/// vem com os keyframes aplicados. Nunca falha: sem fonte, o cabeçalho sai
/// com 0 pontos e o shader cai na caixa do emissor.
void build_frame(const BuildContext& ctx, const Layer& particleLayer, const ParticleData& pd,
                 StaticCache& cache, FrameData& out) noexcept;

/// Buffers de GPU por camada: [kRing cabeçalhos][estático]. O estático só é
/// reescrito quando a chave muda (buffer novo; o velho sai depois da GPU).
class GpuBuffers {
public:
    /// Escreve o cabeçalho do quadro (e o estático, se mudou). Devolve o
    /// buffer e o índice (vec4) do cabeçalho deste quadro; inválido = falhou.
    [[nodiscard]] BufferHandle upload(GPUBackend& backend, const FrameData& fd, u64 frameNumber,
                                      u32& headerBase) noexcept;
    void collect(GPUBackend& backend, u64 frameNumber, u64 age = 240) noexcept;
    void release(GPUBackend& backend) noexcept;
    void forget() noexcept { entries_.clear(); }
    [[nodiscard]] u64 resident_bytes() const noexcept;
private:
    struct Entry {
        BufferHandle buffer{};
        u64 bytes = 0;
        u64 staticKey = ~0ull;
        u32 ring = 0;
        u64 lastFrame = 0;
    };
    std::unordered_map<u64, Entry> entries_;
};

/// Chave de conteúdo do que a camada `src` oferece como fonte de emissão
/// (0 = nada a emitir). Exposta para os testes de cache.
[[nodiscard]] u64 source_key(const Layer& src, u32 emitterType, u32 emitFrom, FrameIndex localTime) noexcept;

} // namespace particles
} // namespace aurea
