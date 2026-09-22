// =============================================================================
//  Aurea / render / FrameGraph.hpp
//
//  AureaFrameGraph — o plano de GPU de um frame.
//
//  Quem monta o frame (compositor, efeitos) DECLARA: "este passe lê A e B e
//  escreve C". O grafo então decide, sozinho:
//
//    - a ORDEM (por dependência, com a ordem de declaração como desempate);
//    - o que PODAR (passe cujo resultado ninguém lê não roda);
//    - o TEMPO DE VIDA de cada textura (primeiro e último uso);
//    - o REAPROVEITAMENTO: duas texturas transitórias cujas vidas não se
//      cruzam usam a MESMA textura física. A textura A morre no passe 2 e a
//      mesma memória vira a textura B no passe 4. Numa cadeia de efeitos em
//      4K isso é a diferença entre caber e ser morto pelo sistema;
//    - as BARREIRAS: cada passe declara como usa cada recurso, e o grafo emite
//      a transição de estado antes dele. Nenhum passe gerencia layout;
//    - os RENDER PASSES: o passe de raster só desenha; begin/end, load op e
//      clear são do grafo.
//
//  Custo por frame: o grafo é remontado do zero a cada frame (é barato: dezenas
//  de passes), mas NÃO aloca em regime — os vetores mantêm a capacidade, os
//  callbacks são `InplaceFunction` e as texturas físicas vêm de um pool que
//  sobrevive entre frames. Em playback estacionário, texturas criadas por frame
//  = 0 (há teste para isso).
// =============================================================================
#pragma once

#include "aurea/core/InplaceFunction.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/render/GPUBackend.hpp"

#include <string>
#include <vector>

namespace aurea {

/// Etapas do pipeline de composição — agrupam o tempo no painel DEV.
enum class PassStage : u8 {
    Upload = 0,
    Decode,        ///< conversão de cor do frame do decoder
    Transform,
    Mask,
    Effects,
    Scene3D,
    Particles,
    Text,
    Composite,
    PostProcess,
    Output,        ///< codificação para o display / encoder
    _Count,
};

[[nodiscard]] constexpr const char* to_string(PassStage s) noexcept {
    switch (s) {
        case PassStage::Upload:      return "upload";
        case PassStage::Decode:      return "cor-do-video";
        case PassStage::Transform:   return "transform";
        case PassStage::Mask:        return "mascara";
        case PassStage::Effects:     return "efeitos";
        case PassStage::Scene3D:     return "3d";
        case PassStage::Particles:   return "particulas";
        case PassStage::Text:        return "texto";
        case PassStage::Composite:   return "composicao";
        case PassStage::PostProcess: return "pos-processo";
        case PassStage::Output:      return "saida";
        case PassStage::_Count:      break;
    }
    return "?";
}

enum class PassKind : u8 { Raster = 0, Compute, Transfer };

/// Referência a um recurso lógico do grafo deste frame.
struct FGTexture {
    u32 index = kInvalidIndex;
    [[nodiscard]] bool valid() const noexcept { return index != kInvalidIndex; }
    friend constexpr bool operator==(FGTexture, FGTexture) noexcept = default;
};

class FrameGraph;

/// O que um passe recebe ao executar.
struct PassContext {
    CommandList& cmds;
    const FrameGraph& graph;
    [[nodiscard]] TextureHandle texture(FGTexture t) const noexcept;
    [[nodiscard]] const TextureDesc& desc(FGTexture t) const noexcept;
};

using PassFn = InplaceFunction<void(PassContext&), 256>;

// -----------------------------------------------------------------------------
// Pool de texturas transitórias. Sobrevive entre frames.
//
// É daqui que o grafo tira as texturas físicas. Uma textura devolvida no fim do
// frame N pode ser pega no frame N+1 sem espera: numa única fila de GPU, a
// barreira do primeiro uso no frame novo já ordena depois das leituras do
// frame velho. O que NÃO pode acontecer é destruir a textura com a GPU ainda
// lendo — e a destruição passa pelo backend, que adia até o fence.
// -----------------------------------------------------------------------------
class TransientTexturePool {
public:
    /// Uma textura parada por mais que isto é destruída. 120 frames = 2 s a
    /// 60 Hz: sobrevive a uma pausa curta, mas não acumula as resoluções de
    /// cada modo de preview que o usuário já testou.
    explicit TransientTexturePool(u32 idleFramesBeforeDestroy = 120) noexcept
        : idleFrames_(idleFramesBeforeDestroy) {}

    void begin_frame(GPUBackend& backend, u64 frameNumber) noexcept;
    [[nodiscard]] TextureHandle acquire(const TextureDesc& desc) noexcept;
    void release(TextureHandle texture) noexcept;
    void end_frame() noexcept;

    /// Destrói tudo. Fechar projeto, perder o dispositivo, encerrar.
    void clear() noexcept;
    /// Esquece tudo SEM destruir: o dispositivo morreu e levou as texturas.
    void forget() noexcept;

    struct Stats {
        u32 alive = 0;
        u32 inUse = 0;
        u64 bytes = 0;
        u32 createdThisFrame = 0;
        u32 destroyedThisFrame = 0;
        u64 createdTotal = 0;
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    struct Entry {
        TextureDesc   desc{};
        TextureHandle texture{};
        u64           lastUsedFrame = 0;
        bool          inUse = false;
    };
    GPUBackend* backend_ = nullptr;
    std::vector<Entry> entries_;
    u64 frame_ = 0;
    u32 idleFrames_ = 120;
    Stats stats_{};
};

// -----------------------------------------------------------------------------
// O grafo
// -----------------------------------------------------------------------------
class FrameGraph {
public:
    FrameGraph();
    FrameGraph(const FrameGraph&) = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;

    // --- Declaração -----------------------------------------------------------

    /// Zera a declaração do frame. Mantém a capacidade dos vetores.
    void reset() noexcept;

    /// Textura transitória: existe só neste frame, o grafo escolhe a física.
    [[nodiscard]] FGTexture create_texture(const char* name, const TextureDesc& desc) noexcept;

    /// Textura que vem de fora (backbuffer, frame importado do decoder, alvo
    /// do export). Nunca é reaproveitada nem criada pelo grafo.
    [[nodiscard]] FGTexture import_texture(const char* name, TextureHandle texture,
                                           const TextureDesc& desc) noexcept;

    /// Passe de raster: desenha em `colorTarget`. O grafo abre e fecha o
    /// render pass com o `load` pedido.
    u32 add_raster_pass(const char* name, PassStage stage, FGTexture colorTarget,
                        LoadOp load, Vec4 clear, PassFn fn) noexcept;
    /// Passe 3D: cor (opcional — inválida = só profundidade, sombra) e
    /// profundidade. `storeDepth` quando outro passe lê a profundidade depois.
    u32 add_raster_pass_depth(const char* name, PassStage stage, FGTexture colorTarget, LoadOp load, Vec4 clear,
                              FGTexture depthTarget, LoadOp depthLoad, bool storeDepth, f32 clearDepth,
                              PassFn fn) noexcept;
    u32 add_compute_pass(const char* name, PassStage stage, PassFn fn) noexcept;
    u32 add_transfer_pass(const char* name, PassStage stage, PassFn fn) noexcept;

    /// Leitura em shader (amostragem).
    void read(u32 pass, FGTexture texture) noexcept;
    /// Escrita de compute em image2D.
    void write_storage(u32 pass, FGTexture texture) noexcept;
    void copy_source(u32 pass, FGTexture texture) noexcept;
    void copy_destination(u32 pass, FGTexture texture) noexcept;

    /// Passe com efeito fora do grafo (leitura de volta, marcação). Nunca é
    /// podado.
    void mark_side_effect(u32 pass) noexcept;

    /// Saída do frame e o estado em que ela deve terminar (`Present` para o
    /// swapchain, `ShaderRead` para uma textura de cache, `TransferSrc` para
    /// leitura de volta). Sem saída, todo passe é podado.
    void set_output(FGTexture texture, ResourceState finalState) noexcept;

    // --- Compilação -----------------------------------------------------------

    /// Ordena, poda, calcula vidas, escolhe texturas físicas e planeja as
    /// barreiras. Falha em ciclo e em leitura de textura nunca escrita.
    [[nodiscard]] Status compile(TransientTexturePool& pool) noexcept;

    // --- Execução -------------------------------------------------------------

    /// Grava os passes na ordem compilada. Com `timers`, cada passe vira uma
    /// medição de GPU com o nome dele.
    void execute(CommandList& cmds, bool timers) noexcept;

    /// Devolve as físicas ao pool. Chamado depois de `execute`.
    void release(TransientTexturePool& pool) noexcept;

    // --- Consultas ------------------------------------------------------------

    [[nodiscard]] TextureHandle physical(FGTexture t) const noexcept;
    [[nodiscard]] const TextureDesc& desc(FGTexture t) const noexcept;

    struct Stats {
        u32 passesDeclared = 0;
        u32 passesExecuted = 0;
        u32 passesCulled = 0;
        u32 transientTextures = 0;   ///< lógicas
        u32 physicalTextures = 0;    ///< distintas usadas neste frame
        u32 aliasedTextures = 0;     ///< lógicas que reusaram física do mesmo frame
        u32 barriers = 0;
        u64 transientBytes = 0;      ///< soma das físicas distintas
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

    /// Ordem de execução (índices de passe), sem os podados.
    [[nodiscard]] const std::vector<u32>& order() const noexcept { return order_; }
    [[nodiscard]] u32 pass_count() const noexcept { return static_cast<u32>(passes_.size()); }
    [[nodiscard]] const char* pass_name(u32 p) const noexcept { return passes_[p].name; }
    [[nodiscard]] PassStage pass_stage(u32 p) const noexcept { return passes_[p].stage; }
    [[nodiscard]] bool pass_culled(u32 p) const noexcept { return passes_[p].culled; }
    [[nodiscard]] u32 resource_count() const noexcept { return static_cast<u32>(resources_.size()); }

    /// Índice da física usada por uma textura lógica (para testes de
    /// aliasing). kInvalidIndex se importada ou podada.
    [[nodiscard]] u32 physical_slot(FGTexture t) const noexcept;

    /// Barreira planejada — exposta para teste.
    struct PlannedBarrier {
        u32 resource = kInvalidIndex;
        ResourceState state = ResourceState::Undefined;
        bool discard = false;
    };
    /// Barreiras emitidas antes do passe `p` (vazio se podado).
    void barriers_before(u32 p, std::vector<PlannedBarrier>& out) const;

    /// Texto do plano, para o painel DEV. Aloca — só sob demanda.
    [[nodiscard]] std::string dump() const;

private:
    enum class Access : u8 { Read = 0, ColorWrite, StorageWrite, CopySrc, CopyDst, DepthWrite };

    struct AccessRecord {
        u32 pass = 0;
        u32 resource = 0;
        Access access = Access::Read;
    };

    struct Resource {
        const char*   name = "";
        TextureDesc   desc{};
        TextureHandle physical{};
        bool          imported = false;
        bool          isOutput = false;
        bool          alive = false;
        ResourceState finalState = ResourceState::ShaderRead;
        u32           firstUse = kInvalidIndex;   ///< posição em order_
        u32           lastUse  = kInvalidIndex;
        u32           slot = kInvalidIndex;       ///< índice em slots_
    };

    struct Pass {
        const char* name = "";
        PassStage   stage = PassStage::Composite;
        PassKind    kind = PassKind::Raster;
        FGTexture   colorTarget{};
        LoadOp      load = LoadOp::Clear;
        FGTexture   depthTarget{};
        LoadOp      depthLoad = LoadOp::Clear;
        bool        storeDepth = false;
        f32         clearDepth = 1.0f;
        f32         clear[4] = {0, 0, 0, 0};
        PassFn      fn;
        bool        sideEffect = false;
        bool        culled = false;
        u32         accessBegin = 0;   ///< faixa em sortedAccess_
        u32         accessCount = 0;
        u32         barrierBegin = 0;  ///< faixa em barriers_
        u32         barrierCount = 0;
    };

    /// Uma textura física deste frame. Várias lógicas podem apontar para ela.
    struct Slot {
        TextureHandle texture{};
        TextureDesc   desc{};
        bool          free = false;   ///< disponível para reuso neste frame
    };

    void add_access(u32 pass, FGTexture t, Access a) noexcept;
    [[nodiscard]] bool sort_passes() noexcept;
    void cull() noexcept;
    [[nodiscard]] Status assign_physical(TransientTexturePool& pool) noexcept;
    void plan_barriers() noexcept;

    std::vector<Pass>          passes_;
    std::vector<Resource>      resources_;
    std::vector<AccessRecord>  accesses_;
    std::vector<AccessRecord>  sortedAccess_;
    std::vector<u32>           order_;
    std::vector<u32>           outputs_;
    std::vector<Slot>          slots_;
    std::vector<PlannedBarrier> barriers_;
    std::vector<PlannedBarrier> finalBarriers_;

    // Temporários da compilação, mantidos para não alocar por frame.
    std::vector<u32> indegree_;
    std::vector<u32> edges_;       ///< pares (pred, succ) achatados
    std::vector<u32> edgeBegin_;   ///< CSR: início dos sucessores de cada passe
    std::vector<u32> succ_;
    std::vector<u32> fill_;
    std::vector<u32> queue_;
    std::vector<ResourceState> tracked_;

    Stats stats_{};
    bool  compiled_ = false;
};

} // namespace aurea
