// =============================================================================
//  Aurea / Engine.hpp
//
//  A fachada. É o ÚNICO cabeçalho que a bridge JNI e a ObjC++ incluem.
//
//  Contrato da fronteira:
//
//   1. NADA de ponteiro do motor cruza para a UI. Só handles (u64) e POD.
//   2. A UI NÃO processa frame e NÃO dita o ritmo do preview. O preview tem a
//      SUA thread de render, pacificada pelo vsync do swapchain; a UI só manda
//      comandos e lê estado. Recomposição do Compose não toca no preview, e um
//      preview pesado não trava a interface.
//   3. Um frame da UI são poucas travessias: `submit_commands` (lock-free),
//      `fill_status` e, no painel DEV, `fill_perf`.
//
//  Threads:
//
//      UI ─ submit_commands ──► fila lock-free ─┐
//      UI ─ fill_status/query_* ─(lock do modelo, curto)
//                                                ▼
//      render ─ [lock do modelo] drena, avança o playback, prepara o snapshot
//             ─ [sem lock] FrameGraph → GPU → swapchain
//      decode (uma por fonte) ─ MediaCodec → cache de frames → acorda o render
// =============================================================================
#pragma once

#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/CommandQueue.hpp"
#include "aurea/command/UndoStack.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/jobs/JobSystem.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/memory/MemoryManager.hpp"
#include "aurea/platform/DeviceCapabilities.hpp"
#include "aurea/playback/Playback.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/GPUBackend.hpp"
#include "aurea/render/RenderScheduler.hpp"
#include "aurea/render/Renderer.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace aurea {

enum class EngineState : u8 {
    Uninitialized = 0,
    Ready,
    Rendering,
    Exporting,
    Suspended,     ///< app em segundo plano
    ShuttingDown,
    Failed,
};

/// O que a UI recebe depois de um frame.
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

struct EngineConfig {
    /// Backend gráfico, criado pela plataforma (Vulkan no Android e no host de
    /// testes, Metal no iOS). O motor assume a posse. Nulo = sem GPU (a
    /// timeline, a animação e a serialização funcionam sem preview).
    GPUBackend* backend = nullptr;
    BackendConfig backendConfig{};

    /// Decoders de mídia da plataforma. NÃO é assumida a posse.
    VideoSourceFactory* mediaFactory = nullptr;

    f32   displayRefreshRate = 60.0f;
    std::string cacheDirectory;
    std::string documentsDirectory;
    u32   workerCount = 0;
    u64   memoryBudgetBytes = 0;
    bool  enableTelemetry = true;
    bool  disableAutosave = false;
    u32   autosaveJournalIntervalMs  = 3000;
    u32   autosaveRecoveryIntervalMs = 60000;
    PreviewScale initialPreviewScale = PreviewScale::Auto;
};

/// Pedido de importação de vídeo.
struct VideoImport {
    std::string sourcePath;    ///< caminho, ou "fd:<n>" no Android
    std::string displayName;
};

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    // =========================================================================
    // Ciclo de vida
    // =========================================================================
    [[nodiscard]] Status initialize(const EngineConfig& config) noexcept;
    void shutdown() noexcept;
    [[nodiscard]] EngineState state() const noexcept;

    /// App em segundo plano: pausa, devolve os decoders de hardware ao sistema,
    /// grava o cache de pipeline. O PROJETO fica intacto.
    [[nodiscard]] Status suspend() noexcept;
    [[nodiscard]] Status resume() noexcept;

    // --- Superfície (Android: SurfaceView → ANativeWindow) -------------------
    /// Chamadas da thread da UI. `detach_surface` só volta depois que a GPU
    /// parou de usar a janela — o Android destrói a superfície logo depois.
    [[nodiscard]] Status attach_surface(void* nativeWindow, u32 width, u32 height) noexcept;
    void detach_surface() noexcept;
    [[nodiscard]] Status resize_surface(u32 width, u32 height) noexcept;

    // --- Thread de render -----------------------------------------------------
    /// Sobe a thread de render própria do preview. Ela dorme quando não há o
    /// que desenhar (pausado, sem mudança) e acorda com comando, frame novo de
    /// vídeo ou mudança de superfície.
    void start_render_thread() noexcept;
    void stop_render_thread() noexcept;
    /// Acorda a thread de render (há algo novo para mostrar).
    void request_render() noexcept;

    // =========================================================================
    // Projeto
    // =========================================================================
    [[nodiscard]] Status new_project(u32 width = 1920, u32 height = 1080,
                                     f64 fps = 30.0, const char* title = nullptr) noexcept;
    [[nodiscard]] Status load_project(const char* path) noexcept;
    [[nodiscard]] Status save_project(const char* path) noexcept;
    [[nodiscard]] Status save_project() noexcept;
    [[nodiscard]] const AutosaveState& autosave_state() const noexcept;
    [[nodiscard]] Status recover_session() noexcept;
    void discard_recovery() noexcept;

    [[nodiscard]] Project* project() noexcept { return project_.get(); }
    [[nodiscard]] const Project* project() const noexcept { return project_.get(); }

    // --- Importação -----------------------------------------------------------
    /// Sonda o arquivo (fora do lock), cria o asset e uma layer de vídeo no
    /// TOPO da composição. Sendo o primeiro clipe, a composição adota tamanho,
    /// fps e duração do vídeo. Devolve o id da layer.
    [[nodiscard]] Result<u64> import_video(const VideoImport& request) noexcept;
    /// Imagem já decodificada pela plataforma (RGBA8 sRGB, alfa reto).
    [[nodiscard]] Result<u64> import_image(const u8* rgba, u32 width, u32 height,
                                           const char* name) noexcept;

    // =========================================================================
    // A fronteira
    // =========================================================================
    [[nodiscard]] u32 submit_commands(const Command* commands, u32 count,
                                      const char* stringBlob = nullptr,
                                      u32 stringBlobSize = 0) noexcept;

    /// Um frame completo: drena, avança o playback, prepara, renderiza e
    /// apresenta. Chamado pela thread de render (ou pelos testes).
    [[nodiscard]] Status render_frame() noexcept;

    /// Renderiza o instante atual numa textura (export, testes visuais), em
    /// resolução cheia, sem superfície. Espera a GPU terminar.
    [[nodiscard]] Status render_offscreen(TextureHandle target, u32 width, u32 height) noexcept;

    [[nodiscard]] EngineStatus read_status() noexcept;
    [[nodiscard]] EngineTelemetry read_telemetry() noexcept;

    void fill_status(bridge::EngineStatusPOD& out) noexcept;
    void fill_telemetry(bridge::TelemetryPOD& out) noexcept;
    void fill_perf(bridge::PerfPOD& out) noexcept;
    void fill_export_progress(bridge::ExportProgressPOD& out) const noexcept;

    // =========================================================================
    // Consultas (somente leitura, para a UI)
    // =========================================================================
    u32 query_layers(bridge::LayerRow* out, u32 capacity,
                     char* outNameBlob, u32 nameBlobCapacity) noexcept;
    u32 query_keyframes(u64 layerId, bridge::KeyframeRow* out, u32 capacity) noexcept;
    u32 query_curve(u64 layerId, u32 property, i32 startFrame, i32 endFrame,
                    f32* outValues, u32 sampleCount) noexcept;

    /// Tipos de efeito disponíveis.
    u32 query_effect_catalog(bridge::EffectCatalogRow* out, u32 capacity,
                             char* blob, u32 blobCapacity) noexcept;
    /// Efeitos aplicados numa layer, na ordem.
    u32 query_layer_effects(u64 layerId, bridge::LayerEffectRow* out, u32 capacity,
                            char* blob, u32 blobCapacity) noexcept;
    /// Parâmetros de um efeito aplicado, com o valor no playhead.
    u32 query_effect_params(u64 layerId, u32 effectId, bridge::EffectParamRow* out, u32 capacity,
                            char* blob, u32 blobCapacity) noexcept;

    // =========================================================================
    // Seleção
    // =========================================================================
    void set_selection(const u64* layerIds, u32 count) noexcept;
    void clear_selection() noexcept;
    [[nodiscard]] u32 selection_count() const noexcept;
    u32 get_selection(u64* out, u32 capacity) const noexcept;
    [[nodiscard]] bool is_selected(u64 layerId) const noexcept;

    // =========================================================================
    // Export (próxima fase: reusa o MESMO renderer via render_offscreen)
    // =========================================================================
    [[nodiscard]] Status start_export(const ExportSettings& settings, const char* outputPath) noexcept;
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
    [[nodiscard]] GPUBackend* gpu() noexcept { return gpu_.get(); }
    [[nodiscard]] Renderer& renderer() noexcept { return renderer_; }
    [[nodiscard]] MediaManager& media() noexcept { return media_; }
    [[nodiscard]] PlaybackController& playback() noexcept { return playback_; }
    [[nodiscard]] CommandQueue& commands() noexcept { return *commandQueue_; }

    /// Executa um comando direto, sem fila. Testes e recuperação de projeto.
    [[nodiscard]] Status apply_command(const Command& cmd, const char* stringData = nullptr) noexcept;

    void debug_feed_frame_stats(const FrameStats& stats) noexcept;

    [[nodiscard]] const std::string& cache_directory() const noexcept { return config_.cacheDirectory; }
    [[nodiscard]] const std::string& documents_directory() const noexcept { return config_.documentsDirectory; }

private:
    [[nodiscard]] Status apply_command_internal(const Command& cmd, const char* stringData,
                                                bool recordUndo) noexcept;
    void drain_commands_locked() noexcept;
    [[nodiscard]] Composition* current_composition() noexcept;
    [[nodiscard]] Status recover_device_locked() noexcept;
    void render_thread_main() noexcept;
    void update_perf(const FrameStats& stats, const RenderTimings& timings,
                     const FrameSnapshot& snap, u64 frameStartNs) noexcept;
    [[nodiscard]] RenderSettings current_render_settings() noexcept;
    static const ImagePixels* image_lookup(void* self, AssetId id);
    static void on_frame_ready(void* self);

    [[nodiscard]] AdaptiveResolutionController& adapt() noexcept { return *adaptive_; }

    EngineConfig       config_{};
    std::atomic<EngineState> state_{EngineState::Uninitialized};
    Errc               lastError_ = Errc::Ok;
    char               lastErrorDetail_[128]{};

    DeviceCapabilities caps_{};
    JobSystem          jobs_{};
    MemoryManager      memory_{};
    UndoStack          undo_{};
    EffectRegistry     effectRegistry_{};
    AdaptiveResolutionController* adaptive_ = nullptr;

    std::unique_ptr<GPUBackend> gpu_;
    Renderer           renderer_;
    MediaManager       media_;
    PlaybackController playback_;
    FrameScheduler     frameScheduler_;
    FrameSnapshot      snapshot_;

    std::unique_ptr<CommandQueue> commandQueue_;
    std::unique_ptr<Project>      project_;
    std::unordered_map<u64, ImagePixels> images_;   ///< por AssetId empacotado

    std::vector<u64> selection_;

    // --- Sincronização --------------------------------------------------------
    /// Protege projeto, timeline, playback, seleção e imagens.
    mutable std::mutex modelMutex_;
    /// Protege backend e renderer (GPU). Nunca adquirido com o do modelo já
    /// preso por outra thread que espere o de render — a ordem é sempre
    /// modelo → render, ou render sozinho.
    std::mutex renderMutex_;

    std::thread renderThread_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    bool wakeFlag_ = false;
    std::atomic<bool> renderRunning_{false};
    std::atomic<bool> playingHint_{false};
    std::atomic<bool> surfaceAttached_{false};

    SurfaceDesc surface_{};

    // --- Métricas ---------------------------------------------------------------
    mutable std::mutex perfMutex_;
    bridge::PerfPOD perf_{};
    FrameStats lastFrame_{};
    u64 frameCounter_ = 0;
    u64 fpsWindowStartNs_ = 0;
    u32 fpsWindowFrames_ = 0;
    f32 measuredFps_ = 0.0f;

    struct ExportContext;
    std::unique_ptr<ExportContext> exportCtx_;
};

} // namespace aurea
