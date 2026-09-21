// =============================================================================
//  Aurea / Engine.hpp
//
//  A fachada. É o ÚNICO cabeçalho que a bridge JNI e a ObjC++ incluem.
//
//  Contrato da fronteira, em três regras:
//
//   1. NADA de ponteiro do motor cruza para a UI. Só handles (u64) e POD.
//      A UI guarda números; quem resolve é o motor.
//
//   2. A UI NÃO processa frame. Ela chama `render_frame` e o resultado vai
//      direto para a superfície nativa. Nenhum bitmap atravessa a fronteira.
//
//   3. A UI esconde o motor: as TRÊS chamadas de uma sessão de edição são
//      `submit_commands`, `render_frame` e `read_telemetry`. Um frame normal
//      são 3 chamadas nativas, não 300.
//
//  Fluxo por frame:
//
//      UI (Compose/SwiftUI)
//        │  escreve comandos POD na fila
//        ├─ submit_commands()  ─────────────► CommandQueue
//        │
//        │                                     Engine aplica
//        │                                     Engine avalia animação
//        │                                     FrameGraph compila
//        ├─ render_frame()     ─────────────► GPU compõe → superfície
//        │
//        └─ read_telemetry()   ◄───────────── FrameStats + AdaptiveState
//
//  Nada bloqueia a UI: `submit_commands` é lock-free, `render_frame` devolve
//  o estado do frame que a GPU ainda está desenhando, e `read_telemetry` é uma
//  leitura de struct POD.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/memory/MemoryManager.hpp"
#include "aurea/jobs/JobSystem.hpp"
#include "aurea/command/CommandQueue.hpp"
#include "aurea/command/UndoStack.hpp"
#include "aurea/platform/DeviceCapabilities.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/GPUBackend.hpp"
#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/EffectGraph.hpp"
#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/render/RenderScheduler.hpp"
#include "aurea/bridge/BridgePods.hpp"

#include <memory>
#include <vector>
#include <functional>

namespace aurea {

/// Estado do motor, exposto à UI sem ponteiro.
enum class EngineState : u8 {
    Uninitialized = 0,
    Ready,
    Rendering,
    Exporting,
    Suspended,     ///< app em background
    ShuttingDown,
    Failed,
};

/// O que a UI recebe depois de um frame. POD — atravessa a bridge por valor.
struct EngineStatus {
    EngineState state = EngineState::Uninitialized;
    Errc lastError = Errc::Ok;
    char lastErrorDetail[128]{};

    f32  currentFps = 0.0f;
    f32  averageFrameMs = 0.0f;
    u32  previewWidth = 0;
    u32  previewHeight = 0;
    u32  previewNumerator = 1;
    u32  previewDenominator = 1;
    bool previewAuto = true;

    FrameIndex playhead{0};
    FrameIndex duration{0};
    bool playing = false;

    u32  layerCount = 0;
    u32  selectedCount = 0;

    bool canUndo = false;
    bool canRedo = false;
    u32  undoDepth = 0;

    u32  assetCount = 0;

    bool dirty = false;
    bool recoveryAvailable = false;

    u32  droppedFrames = 0;
    f32  gpuMs = 0.0f;
    f32  cpuMs = 0.0f;
    f32  decodeMs = 0.0f;
    u32  passesExecuted = 0;
    u32  passesCulled = 0;
    f32  cacheHitRate = 0.0f;
    u64  gpuMemoryBytes = 0;
    u64  cpuMemoryBytes = 0;
    f32  memoryPressure = 0.0f;
};

/// Telemetria completa — só o painel de debug lê isto.
struct EngineTelemetry {
    FrameStats frame{};

    u32  workerCount = 0;
    u64  jobsCompleted = 0;
    u32  queueDepth[static_cast<u8>(JobPriority::Count)]{};

    u32  shaderCount = 0;
    u32  pipelineCount = 0;
    f32  pipelineHitRate = 0.0f;
    u32  shaderFailures = 0;

    u32  physicalResources = 0;
    u32  logicalResources = 0;

    u32  frameCacheEntries = 0;
    f32  frameCacheHitRate = 0.0f;

    u32  adaptiveScaleChanges = 0;
    u32  effectsInPreviewMode = 0;

    u64  undoBlobBytes = 0;
    u64  commandsDropped = 0;

    ThermalState::Level thermal = ThermalState::Level::Unknown;
    bool throttling = false;
};

/// Opções de inicialização. A plataforma preenche o que sabe; o motor descobre
/// o resto por conta própria.
struct EngineConfig {
    /// Janela nativa de apresentação (ANativeWindow* / CAMetalLayer*).
    void* nativeWindow = nullptr;
    u32   surfaceWidth = 0;
    u32   surfaceHeight = 0;
    f32   displayRefreshRate = 60.0f;
    bool  displaySupportsHdr = false;

    /// Diretório onde ficam cache, proxy e temporários. A plataforma informa o
    /// caminho da sandbox — o motor nunca adivinha.
    std::string cacheDirectory;
    std::string documentsDirectory;

    /// Workers. 0 = automático por DeviceCapabilities.
    u32   workerCount = 0;

    /// Se true, o motor cria o backend gráfico. Em testes headless, false —
    /// a timeline, a animação e a serialização funcionam sem GPU.
    bool  createGpuBackend = true;

    /// Orçamento de memória em bytes. 0 = automático.
    u64   memoryBudgetBytes = 0;

    /// Habilita o painel de telemetria (timestamps na GPU, contadores). Custa
    /// um pouco; fica desligado em release.
    bool  enableTelemetry = false;

    /// Desliga o autosave e a recuperação. Usado pelos testes (que não podem
    /// escrever no diretório de trabalho do usuário) e pelo modo headless.
    bool  disableAutosave = false;

    /// Cadências do autosave, em milissegundos. O journal é barato e roda
    /// seguido; o ponto de recuperação completo custa mais e roda espaçado.
    u32   autosaveJournalIntervalMs  = 3000;
    u32   autosaveRecoveryIntervalMs = 60000;

    /// Escala de qualidade inicial do preview. AUTO deixa o controlador
    /// adaptativo decidir desde o primeiro frame.
    PreviewScale initialPreviewScale = PreviewScale::Auto;
};

/// O motor.
///
/// Uma instância por processo. Não é singleton global escondido: quem cria é
/// quem destrói, e o ciclo de vida é explícito. (A UI tem UMA instância, mas
/// isso é decisão dela, não do motor — os testes criam várias.)
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    // =========================================================================
    // Ciclo de vida
    // =========================================================================

    /// Sobe o motor: detecta capacidades, cria backend, sobe o pool, prepara
    /// caches. Pode levar dezenas de ms — chame fora da thread da UI.
    [[nodiscard]] Status initialize(const EngineConfig& config) noexcept;

    /// Encerra tudo. Espera o trabalho em curso terminar; chamadas pendentes
    /// devolvem `ShuttingDown`.
    void shutdown() noexcept;

    [[nodiscard]] EngineState state() const noexcept;

    // --- Suspensão (app em background) ---------------------------------------
    //
    //  O sistema pode tirar a GPU e o surface da gente a qualquer momento. Em
    //  vez de tentar desenhar num surface morto e ser morto pelo sistema,
    //  `suspend` libera GPU e caches descartáveis, e `resume` recria.
    //  O PROJETO permanece em memória — suspender não perde trabalho.

    [[nodiscard]] Status suspend() noexcept;
    [[nodiscard]] Status resume(const EngineConfig& config) noexcept;

    /// A superfície mudou de tamanho (rotação, split-screen, redimensionar).
    [[nodiscard]] Status resize_surface(u32 width, u32 height) noexcept;

    // =========================================================================
    // Projeto
    // =========================================================================

    /// Projeto novo, com uma composição pronta.
    [[nodiscard]] Status new_project(u32 width = 1920, u32 height = 1080,
                                     f64 fps = 60.0,
                                     const char* title = nullptr) noexcept;

    [[nodiscard]] Status load_project(const char* path) noexcept;
    [[nodiscard]] Status save_project(const char* path) noexcept;

    /// Salva no caminho atual. Se o projeto nunca foi salvo, devolve
    /// `InvalidState` — a UI precisa então pedir um destino.
    [[nodiscard]] Status save_project() noexcept;

    /// Estado do autosave + recuperação. A UI chama isto ao abrir para saber
    /// se deve oferecer "recuperar sessão anterior".
    [[nodiscard]] const AutosaveState& autosave_state() const noexcept;

    [[nodiscard]] Status recover_session() noexcept;
    void discard_recovery() noexcept;

    [[nodiscard]] Project* project() noexcept { return project_.get(); }
    [[nodiscard]] const Project* project() const noexcept { return project_.get(); }

    // =========================================================================
    // A fronteira — três chamadas por frame
    // =========================================================================

    /// Escreve um lote de comandos. Lock-free: a UI nunca espera o motor.
    ///
    /// Devolve a quantidade aceita. Menos que `count` significa fila cheia —
    /// a UI deve reenviar o resto no próximo frame. Nunca bloqueia, nunca
    /// perde comando silenciosamente.
    [[nodiscard]] u32 submit_commands(const Command* commands, u32 count,
                                      const char* stringBlob = nullptr,
                                      u32 stringBlobSize = 0) noexcept;

    /// Desenha um frame.
    ///
    /// Faz, nesta ordem: drena a fila de comandos, atualiza o relógio, avalia
    /// a animação das layers ativas, compila o FrameGraph, executa na GPU e
    /// apresenta. NÃO bloqueia esperando a GPU terminar o frame anterior —
    /// quem espera é `present`, dois frames depois.
    ///
    /// `audioTime` é a posição do mixer. Durante playback é o master clock; se
    /// o áudio não estiver pronto, o motor cai no relógio do sistema por um
    /// frame e registra isso na telemetria.
    [[nodiscard]] Status render_frame(TickNs audioTime) noexcept;

    /// Estado condensado para a UI. POD, sem alocação.
    [[nodiscard]] EngineStatus read_status() noexcept;

    /// Telemetria completa. Só o painel de debug chama.
    [[nodiscard]] EngineTelemetry read_telemetry() noexcept;

    // --- Versões para a fronteira nativa -------------------------------------
    //
    //  Escrevem direto no struct da bridge, que é o que a UI lê por offset.
    //  Separadas das versões internas de propósito: os tipos da bridge são
    //  congelados por static_assert, e mudar um campo interno aqui NÃO pode
    //  quebrar a UI. O preço é uma cópia campo a campo por frame — dezenas de
    //  instruções, irrelevante diante do trabalho de composição.

    void fill_status(bridge::EngineStatusPOD& out) noexcept;
    void fill_telemetry(bridge::TelemetryPOD& out) noexcept;
    void fill_export_progress(bridge::ExportProgressPOD& out) const noexcept;

    // =========================================================================
    // Consultas à timeline (somente leitura, para desenhar a UI)
    // =========================================================================
    //
    //  A UI pede os dados que precisa desenhar numa única chamada, num buffer
    //  que ela mesma fornece. Não há uma chamada por layer — seriam 200
    //  travessias de bridge por frame só para desenhar a lista.

    /// Preenche `out` com as layers da composição atual. Devolve quantas.
    /// `outNameBlob` recebe os nomes concatenados.
    ///
    /// Os dois tipos são os da fronteira (bridge/BridgePods.hpp), não structs
    /// internas: a UI lê estes offsets, e eles são congelados por static_assert.
    u32 query_layers(bridge::LayerRow* out, u32 capacity,
                     char* outNameBlob, u32 nameBlobCapacity) noexcept;

    /// Keyframes de uma layer, para a timeline desenhar a barra de keyframes
    /// sem uma chamada por propriedade.
    u32 query_keyframes(u64 layerId, bridge::KeyframeRow* out, u32 capacity) noexcept;

    /// Caminho da curva de uma propriedade, amostrado em N pontos — é o que o
    /// graph editor desenha. Amostrar no motor garante que o gráfico mostra
    /// exatamente o que a avaliação produz, sem reimplementar a curva na UI.
    u32 query_curve(u64 layerId, u32 property, i32 startFrame, i32 endFrame,
                    f32* outValues, u32 sampleCount) noexcept;

    // =========================================================================
    // Seleção
    // =========================================================================
    //
    //  A seleção mora no motor porque undo/redo, comandos em lote e o painel
    //  de propriedades precisam dela de forma consistente. Se morasse na UI, o
    //  motor não saberia o que "aplicar a todas as selecionadas" significa.

    void set_selection(const u64* layerIds, u32 count) noexcept;
    void clear_selection() noexcept;
    [[nodiscard]] u32 selection_count() const noexcept;
    u32 get_selection(u64* out, u32 capacity) const noexcept;
    [[nodiscard]] bool is_selected(u64 layerId) const noexcept;

    // =========================================================================
    // Export
    // =========================================================================

    /// Inicia uma exportação. Devolve imediatamente — o export roda no pool.
    /// O progresso vem por `export_progress`.
    [[nodiscard]] Status start_export(const ExportSettings& settings,
                                      const char* outputPath) noexcept;

    [[nodiscard]] Status cancel_export() noexcept;

    struct ExportProgress {
        bool  running = false;
        bool  finished = false;
        Errc  result = Errc::Ok;
        u32   framesTotal = 0;
        u32   framesDone = 0;
        f32   fps = 0.0f;
        u32   etaSeconds = 0;
        char  message[128]{};
    };
    [[nodiscard]] ExportProgress export_progress() const noexcept;

    // =========================================================================
    // Acesso de baixo nível (testes e painel de debug)
    // =========================================================================

    [[nodiscard]] const DeviceCapabilities& caps() const noexcept { return caps_; }
    [[nodiscard]] JobSystem& jobs() noexcept { return jobs_; }
    [[nodiscard]] MemoryManager& memory() noexcept { return memory_; }
    [[nodiscard]] UndoStack& undo() noexcept { return undo_; }
    [[nodiscard]] EffectRegistry& effects() noexcept { return effectRegistry_; }
    [[nodiscard]] GPUBackend* gpu() noexcept { return gpu_; }
    [[nodiscard]] CommandQueue& commands() noexcept { return *commandQueue_; }

    /// Executa um comando direto, sem passar pela fila. Só para testes e para
    /// o caminho de recuperação de projeto. A UI SEMPRE usa `submit_commands`.
    [[nodiscard]] Status apply_command(const Command& cmd,
                                       const char* stringData = nullptr) noexcept;

    /// Recalcula a escala adaptativa com um frame simulado. Só para testes —
    /// em produção o controlador é alimentado pelo renderer.
    void debug_feed_frame_stats(const FrameStats& stats) noexcept;

    /// Caminho de cache. Usado pelas camadas de mídia da plataforma.
    [[nodiscard]] const std::string& cache_directory() const noexcept { return config_.cacheDirectory; }
    [[nodiscard]] const std::string& documents_directory() const noexcept { return config_.documentsDirectory; }

private:
    friend class CommandApplier;

    [[nodiscard]] Status apply_command_internal(const Command& cmd,
                                                const char* stringData,
                                                bool recordUndo) noexcept;
    [[nodiscard]] Status drena_comandos() noexcept;
    void update_clock(TickNs audioTime) noexcept;
    void evaluate_animation(FrameIndex time) noexcept;
    void rebuild_engine_status() noexcept;
    [[nodiscard]] Composition* current_composition() noexcept;
    [[nodiscard]] Status ensure_export_worker() noexcept;

    /// Controlador adaptativo. Devolve uma referência sempre válida: ele é
    /// criado no construtor porque as consultas de resolução são feitas antes
    /// de `initialize` (a UI desenha o preview vazio usando a escala).
    [[nodiscard]] AdaptiveResolutionController& adapt() noexcept { return *adaptive_; }

    EngineConfig       config_{};
    EngineState        state_ = EngineState::Uninitialized;
    Errc               lastError_ = Errc::Ok;
    char               lastErrorDetail_[128]{};

    DeviceCapabilities caps_{};
    JobSystem          jobs_{};
    MemoryManager      memory_{};
    UndoStack          undo_{};
    EffectRegistry     effectRegistry_{};
    ShaderLibrary      shaders_{};
    FrameGraph         frameGraph_;
    FrameCache         frameCache_{};
    FramePrefetcher    prefetcher_{};
    AdaptiveResolutionController* adaptive_ = nullptr;

    GPUBackend*        gpu_ = nullptr;
    std::unique_ptr<CommandQueue> commandQueue_;
    std::unique_ptr<Project>      project_;

    /// Layers ativas do frame atual. Reusado entre frames — o vetor mantém a
    /// capacidade e nunca realoca durante o playback.
    std::vector<LayerId> activeLayers_;

    /// Seleção. Vetor ordenado por id para `is_selected` ser busca binária.
    std::vector<u64> selection_;

    FrameStats      lastFrame_{};
    EngineStatus    cachedStatus_{};
    EngineTelemetry cachedTelemetry_{};

    u64 lastSubmitNs_ = 0;
    u64 frameCounter_ = 0;
    u32 droppedFrames_ = 0;

    u64 exportRevision_ = 0;

    // Implementação do export fica em TranslationUnits separadas.
    struct ExportContext;
    std::unique_ptr<ExportContext> exportCtx_;
};

} // namespace aurea
