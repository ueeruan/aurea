// =============================================================================
//  Aurea / render / FrameGraph.hpp
//
//  O grafo de um frame.
//
//  A ideia central: o compositor NÃO executa passes na ordem em que foram
//  declarados. Ele declara o que cada passe lê e escreve, e o grafo resolve:
//
//     - ordem de execução (por dependência, não por declaração);
//     - quem pode rodar em paralelo (passe de áudio não disputa GPU);
//     - quais recursos precisam existir e por quanto tempo;
//     - onde reinserir barreiras de sincronização;
//     - quais passes podem ser ELIMINADOS (ninguém lê o resultado → não roda).
//
//  O ganho concreto num editor: uma layer com opacidade 0, ou fora da tela, ou
//  coberta por outra opaca, tem seu subgrafo inteiro podado antes de custar
//  qualquer coisa. Sem isso, cada layer invisível ainda paga decode + efeitos.
//
//  Sobre memória: recursos do grafo são ALIADOS por tempo de vida. Dois
//  intermediários com vidas que não se sobrepõem dividem a mesma textura. Numa
//  cadeia de 8 efeitos sobre 4K, isso derruba o pico de ~600 MB para ~150 MB —
//  e é a diferença entre rodar e ser morto pelo sistema.
// =============================================================================
#pragma once

#include "aurea/render/GPUBackend.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/core/Result.hpp"

#include <vector>
#include <string>

namespace aurea {

/// Etapas nomeadas do pipeline de composição. A ordem aqui é INFORMATIVA — quem
/// manda é a dependência declarada — mas serve para o painel de telemetria
/// agrupar o tempo gasto por etapa.
///
///   Decode → TimeRemap → Transform → Mask → Effects → 3D → Particles
///          → Text → Composite → ColorOutput → Display
enum class PassStage : u8 {
    Decode = 0,
    TimeRemap,
    Transform,
    Mask,
    Effects,
    Scene3D,
    Particles,
    Text,
    Composite,
    PostProcess,
    ColorOutput,
    Present,
    _Count,
};

[[nodiscard]] constexpr const char* to_string(PassStage s) noexcept {
    switch (s) {
        case PassStage::Decode:      return "decode";
        case PassStage::TimeRemap:   return "time-remap";
        case PassStage::Transform:   return "transform";
        case PassStage::Mask:        return "mascara";
        case PassStage::Effects:     return "efeitos";
        case PassStage::Scene3D:     return "3d";
        case PassStage::Particles:   return "particulas";
        case PassStage::Text:        return "texto";
        case PassStage::Composite:   return "composicao";
        case PassStage::PostProcess: return "pos-processo";
        case PassStage::ColorOutput: return "saida-de-cor";
        case PassStage::Present:     return "apresentacao";
        case PassStage::_Count:      break;
    }
    return "?";
}

/// Recurso do grafo. Uma textura (ou buffer) com tempo de vida declarado.
struct FrameResource {
    /// Nome do recurso: "cor-da-layer-3", "mascara-1", "profundidade-3d".
    /// Existe só para o painel de debug — o grafo casa por handles.
    std::string  name;

    // Quem escreve primeiro e quem lê por último. O par define o tempo de vida
    // e é o que permite o aliasing.
    u32 firstPass = kInvalidIndex;
    u32 lastPass  = kInvalidIndex;

    TextureDesc  desc{};
    TextureHandle physical{};   ///< textura real, possivelmente compartilhada
    BufferHandle  buffer{};     ///< quando o recurso é buffer, não textura
    bool          isBuffer = false;

    /// Recurso externo: veio de fora do grafo (backbuffer, frame decodificado).
    /// Nunca é aliado nem criado pelo grafo.
    bool          external = false;
    /// Recurso de histórico (frame anterior): precisa sobreviver ao frame.
    /// É o que permite efeito temporal, eco, motion blur acumulativo.
    bool          persistent = false;

    /// Caminho de importação zero-copy, quando é frame de decoder.
    ExternalImageHandle externalImage{};

    [[nodiscard]] bool alive_at(u32 passIndex) const noexcept {
        return passIndex >= firstPass && passIndex <= lastPass;
    }
};

/// Acesso declarado por um passe.
struct ResourceAccess {
    enum class Type : u8 { Read = 0, Write, ReadWrite };
    u32  resourceIndex = kInvalidIndex;
    Type type = Type::Read;
};

/// Um nó do grafo. `execute` é o que o renderer chama.
class FrameGraph;
using PassExecuteFn = void (*)(FrameGraph& graph, void* userData, CommandList& cmds);

struct FramePass {
    std::string   name;
    PassStage     stage = PassStage::Composite;

    std::vector<u32> reads;
    std::vector<u32> writes;

    PassExecuteFn execute = nullptr;
    void*         userData = nullptr;

    /// Culling declarado. Se o passe não contribui para nenhuma saída, o grafo
    /// o remove — e a decisão fica onde ela pertence, no passe, não espalhada
    /// pelo chamador.
    bool culled = false;

    /// Otimização: este passe não depende de nada do frame e pode rodar uma vez
    /// e ser reaproveitado (ex.: geração de mipmap de um asset estático).
    bool cacheable = false;

    /// Para a telemetria: custo estimado em "unidades de trabalho de fragmento".
    /// É uma estimativa derivada da resolução e do número de amostras — nunca
    /// um número inventado de tempo.
    u64 estimatedCost = 0;

    /// Medição real, quando timestamp queries estão disponíveis. Zero significa
    /// "não medido", não "instantâneo".
    f32 measuredMs = 0.0f;
};

/// Objetivo final do grafo. O grafo só mantém os passes que contribuem para
/// algum destes recursos.
struct FrameGraphOutput {
    u32 resourceIndex = kInvalidIndex;
};

class FrameGraph {
public:
    /// Capacidade inicial. Reservada na construção para que montar o grafo de
    /// um frame não aloque — 60 vezes por segundo, isso importa.
    static constexpr u32 kMaxPasses    = 512;
    static constexpr u32 kMaxResources = 512;

    explicit FrameGraph(usize arenaBytes = 0);

    FrameGraph(const FrameGraph&)            = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;

    // --- Montagem (fase de declaração) ---------------------------------------

    /// Zera o grafo para o próximo frame. Não libera texturas já alocadas: o
    /// pool de recursos sobrevive entre frames, porque realocar 4K a 60 Hz
    /// seria o gargalo dominante.
    void reset() noexcept;

    /// Declara um recurso. Devolve o índice para referência nos passes.
    [[nodiscard]] u32 create_texture(std::string name, const TextureDesc& desc) noexcept;
    [[nodiscard]] u32 create_buffer(std::string name, usize bytes, u32 usage) noexcept;

    /// Declara um recurso que vem de fora (backbuffer, imagem de decoder).
    [[nodiscard]] u32 import_texture(std::string name, TextureHandle external) noexcept;
    [[nodiscard]] u32 import_external_image(std::string name, const ExternalImageHandle& img,
                                            const TextureDesc& desc) noexcept;

    /// Declara um recurso persistente entre frames (histórico para efeito
    /// temporal). É a exceção ao "tudo é reciclado por frame".
    [[nodiscard]] u32 create_persistent_texture(std::string name, const TextureDesc& desc) noexcept;

    /// Adiciona um passe. Devolve o índice.
    [[nodiscard]] u32 add_pass(std::string name, PassStage stage,
                               PassExecuteFn fn, void* userData) noexcept;

    void pass_reads(u32 passIndex, u32 resourceIndex) noexcept;
    void pass_writes(u32 passIndex, u32 resourceIndex) noexcept;
    void pass_reads_writes(u32 passIndex, u32 resourceIndex) noexcept;

    /// Declara a saída final. Sem isto, todo passe é podado.
    void set_output(u32 resourceIndex) noexcept;

    // --- Compilação -----------------------------------------------------------

    /// Resolve dependências, ordena topologicamente, poda o que não contribui,
    /// aloca recursos com aliasing e insere as barreiras.
    ///
    /// É chamada uma vez por frame, depois da montagem e antes da execução.
    /// Não é chamada durante playback se `revision` não mudou — o plano do
    /// frame anterior é reaproveitado, que é o caso comum.
    [[nodiscard]] Status compile(GPUBackend& backend) noexcept;

    /// Revisão da estrutura. Muda quando passes ou recursos são adicionados,
    /// removidos ou religados. `compile` só refaz o trabalho quando muda.
    [[nodiscard]] u64 revision() const noexcept { return revision_; }

    // --- Execução -------------------------------------------------------------

    /// Executa os passes na ordem compilada, gravando os comandos.
    /// Requer `compile()` bem-sucedido e recursos resolvidos.
    void execute(CommandList& cmds) noexcept;

    // --- Consultas e depuração ------------------------------------------------

    [[nodiscard]] u32 pass_count() const noexcept { return passCount_; }
    [[nodiscard]] const FramePass& pass(u32 i) const noexcept { return passes_[i]; }
    [[nodiscard]] u32 resource_count() const noexcept { return resourceCount_; }
    [[nodiscard]] const FrameResource& resource(u32 i) const noexcept { return resources_[i]; }

    /// Ordem de execução resolvida. O painel de telemetria mostra isto.
    [[nodiscard]] const std::vector<u32>& execution_order() const noexcept { return order_; }

    /// Quantos passes foram podados nesta compilação. Se o número for alto, o
    /// grafo está fazendo o trabalho dele.
    [[nodiscard]] u32 culled_count() const noexcept { return culledCount_; }
    [[nodiscard]] u32 merged_count() const noexcept { return mergedCount_; }

    /// Quantos recursos físicos foram alocados versus quantos recursos lógicos
    /// foram declarados. A diferença é o ganho do aliasing.
    [[nodiscard]] u32 physical_resource_count() const noexcept { return physicalCount_; }

    /// Textura resolvida de um recurso lógico.
    [[nodiscard]] TextureHandle texture(u32 resourceIndex) const noexcept;

    [[nodiscard]] u32 find_resource(const char* name) const noexcept;
    [[nodiscard]] u32 find_pass(const char* name) const noexcept;

    /// Grafo em texto, para o painel de debug. Só é gerado quando pedido —
    /// montar isso a cada frame custaria mais que o próprio render.
    [[nodiscard]] std::string dump() const;

    /// Libera todas as texturas físicas. Chamado ao fechar projeto ou quando o
    /// dispositivo é perdido (as texturas do driver antigo morreram).
    void release_gpu_resources(GPUBackend& backend) noexcept;

private:
    /// Recurso físico: a textura/buffer real do driver. Vários recursos
    /// lógicos podem apontar para o mesmo físico (aliasing), desde que seus
    /// tempos de vida não se sobreponham.
    struct PhysicalResource {
        TextureDesc  desc{};
        TextureHandle texture{};
        BufferHandle  buffer{};
        bool          isBuffer = false;

        /// Fim do último passe que usou este físico na alocação atual. É o que
        /// decide se ele está livre para o próximo recurso lógico.
        u32           busyUntilPass = kInvalidIndex;

        /// Nome do recurso lógico que motivou a criação. Serve só para a
        /// mensagem de erro apontar onde o grafo falhou.
        std::string   ownerName;
    };

    void topo_sort() noexcept;
    void cull_unreachable() noexcept;
    void alias_resources() noexcept;
    void insert_barriers() noexcept;

    std::vector<FramePass>     passes_;
    std::vector<FrameResource> resources_;
    std::vector<PhysicalResource> physical_;
    std::vector<u32>           order_;
    std::vector<u32>           outputs_;
    std::vector<u8>            marks_;      ///< visitado / na recursão, para detectar ciclo

    /// Mapa recurso lógico → índice em `physical_`. -1 = sem recurso físico
    /// (externo, ou podado).
    std::vector<i32>           resourceToPhysical_;

    u32  passCount_ = 0;
    u32  resourceCount_ = 0;
    u32  physicalCount_ = 0;
    u32  culledCount_ = 0;
    u32  mergedCount_ = 0;
    u64  revision_ = 1;
    u64  compiledRevision_ = 0;
    bool compiled_ = false;
};

} // namespace aurea
