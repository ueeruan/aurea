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

#include "aurea/export/ExportSink.hpp"
#include "aurea/scene3d/Importer.hpp"

#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/CommandQueue.hpp"
#include "aurea/command/History.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/jobs/JobSystem.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/media/ThumbnailService.hpp"
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
    f64  compFps = 0.0;
    u32  compWidth = 0;
    u32  compHeight = 0;

    u32  layerCount = 0;
    u32  selectedCount = 0;
    u32  thumbnailGeneration = 0;
    u32  modelRevision = 0;

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

/// Decodifica a imagem de um asset (caminho ou URI da plataforma) em RGBA8
/// sRGB com alfa reto. É a plataforma que sabe abrir `content://` e decodificar
/// JPEG/PNG/HEIC; o motor só guarda a origem no projeto e pede de volta ao abrir.
using ImageLoaderFn = bool (*)(const char* sourcePath, ImagePixels& out, void* ctx);

struct EngineConfig {
    /// Backend gráfico, criado pela plataforma (Vulkan no Android e no host de
    /// testes, Metal no iOS). O motor assume a posse. Nulo = sem GPU (a
    /// timeline, a animação e a serialização funcionam sem preview).
    GPUBackend* backend = nullptr;
    BackendConfig backendConfig{};

    /// Decoders de mídia da plataforma. NÃO é assumida a posse.
    VideoSourceFactory* mediaFactory = nullptr;
    /// Decodificador de imagem da plataforma (reabrir projeto com imagens).
    ImageLoaderFn imageLoader = nullptr;
    void* imageLoaderContext = nullptr;

    /// Encoder/contêiner da plataforma para o export. Nulo = export
    /// indisponível (recusado com NotSupported, nunca fingido).
    ExportSinkFactory exportSinkFactory = nullptr;
    void* exportSinkContext = nullptr;

    /// Saída de som da plataforma (AAudio no Android). NÃO é assumida a posse.
    /// Nula = preview mudo; o relógio do sistema conduz o playback e o export
    /// continua mixando o áudio normalmente.
    audio::AudioOutput* audioOutput = nullptr;

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

/// Pedido de importação de modelo 3D (glTF/GLB). `path` é um arquivo no
/// sandbox do app (a plataforma copia para lá o que vem de `content://`):
/// dentro de `documentsDirectory`, o projeto guarda o caminho RELATIVO — o
/// mesmo .aurea abre no Android e no iOS.
struct ModelImport {
    std::string path;
    std::string displayName;
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
    /// Redesenha e reapresenta mesmo sem mudança no modelo: a janela voltou a
    /// aparecer (seletor do sistema fechou) e o último quadro apresentado com
    /// ela escondida pode ter sido descartado pelo compositor.
    void invalidate() noexcept;

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
    /// Arquivo de áudio (m4a, mp3, wav, aac, ogg, flac…): camada de áudio no
    /// topo, a partir do início. Sendo o primeiro conteúdo, a composição adota
    /// a duração do som.
    [[nodiscard]] Result<u64> import_audio(const VideoImport& request) noexcept;
    /// "Extrair o áudio": o som de um vídeo vira uma camada própria (mesmo
    /// tempo, mesmo corte) e o vídeo fica mudo. Uma ação de desfazer.
    [[nodiscard]] Result<u64> extract_audio(u64 videoLayerId) noexcept;
    /// "Congelar quadro": divide o clipe no `frame`, insere ali `holdFrames`
    /// do quadro parado e empurra o resto do clipe para depois. Uma ação de
    /// desfazer. Devolve o id do clipe congelado.
    [[nodiscard]] Result<u64> freeze_frame(u64 layerId, i64 frame, i64 holdFrames) noexcept;
    /// Nova forma no centro da composição, do cabeçote até o fim. `preset` é
    /// o ladrilho da aba Forma (0..14). Devolve o id da camada.
    [[nodiscard]] Result<u64> add_shape(u32 preset) noexcept;
    /// Imagem já decodificada pela plataforma (RGBA8 sRGB, alfa reto).
    /// `sourcePath` (URI/caminho) fica no projeto: ao reabrir, o motor pede a
    /// imagem de novo ao `imageLoader`. Sem origem, a imagem só vive na sessão.
    [[nodiscard]] Result<u64> import_image(const u8* rgba, u32 width, u32 height,
                                           const char* name, const char* sourcePath = nullptr) noexcept;
    /// Importa um modelo 3D e cria a layer no topo, enquadrada. O parse roda
    /// na thread de quem chama (a plataforma chama fora da UI); o modelo só
    /// trava no fim, para criar asset e layer. Falha = erro específico
    /// (`detail` preenchido), nunca "importado" com a tela preta.
    [[nodiscard]] Result<u64> import_model(const ModelImport& request, scene3d::ImportProgress* progress = nullptr,
                                           std::string* detail = nullptr) noexcept;
    /// Asset 3D carregado (nulo = ausente/ilegível). Compartilhado: a layer
    /// apagada não invalida quem ainda desenha.
    [[nodiscard]] std::shared_ptr<const scene3d::SceneAsset> model_asset(u64 assetId) const noexcept;

    // =========================================================================
    // A fronteira
    // =========================================================================
    [[nodiscard]] u32 submit_commands(const Command* commands, u32 count,
                                      const char* stringBlob = nullptr,
                                      u32 stringBlobSize = 0) noexcept;

    /// Um frame completo: drena, avança o playback, prepara, renderiza e
    /// apresenta. Chamado pela thread de render (ou pelos testes).
    ///
    /// `onlyIfChanged` (thread de render): não redesenha quando nada mudou —
    /// mesmo frame do playhead, sem comando novo, sem frame de vídeo que
    /// faltava. Um vídeo de 30 fps num painel de 120 Hz redesenha 30 vezes
    /// por segundo, não 120.
    [[nodiscard]] Status render_frame(bool onlyIfChanged = false) noexcept;

    /// Renderiza o instante atual numa textura (export, testes visuais), em
    /// resolução cheia, sem superfície. Espera a GPU terminar.
    [[nodiscard]] Status render_offscreen(TextureHandle target, u32 width, u32 height) noexcept;

    /// O frame do playhead em RGBA8 sRGB (alfa reto), com o lado maior em
    /// `maxDim`. Miniatura do projeto na Home. Síncrono (espera a GPU).
    [[nodiscard]] Status capture_frame_rgba(u32 maxDim, std::vector<u8>& out, u32& width, u32& height) noexcept;

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
    /// Miniatura da layer no instante `timelineFrame` (RGBA8 sRGB, `height`
    /// linhas). Devolve os bytes escritos, ou 0 se ainda não está pronta (o
    /// pedido fica na fila; `thumbnailGeneration` no status avisa quando
    /// chegar). `outWidth` recebe a largura.
    u32 query_thumbnail(u64 layerId, i32 timelineFrame, u32 height, u8* out, u32 capacity,
                        u32* outWidth) noexcept;

    /// Composição atual: id empacotado e ajustes (tamanho, fps, duração em
    /// frames, fundo RGBA linear). false = sem projeto.
    bool query_composition(u64& id, u32& width, u32& height, f64& fps, i64& durationFrames,
                           f32 background[4]) noexcept;
    /// Teto de tamanho de composição do aparelho, como lado maior × lado
    /// menor (a mesma regra com que CompositionSetSize recusa).
    void composition_size_cap(u32& longSide, u32& shortSide) const noexcept;

    /// Detalhe de uma camada no playhead. false = camada não existe.
    bool query_layer_detail(u64 layerId, bridge::LayerDetailPOD& out) noexcept;
    u32 query_curve(u64 layerId, u32 property, i32 startFrame, i32 endFrame,
                    f32* outValues, u32 sampleCount) noexcept;
    /// Waveform da camada: `count` baldes a partir do frame `startFrame` da
    /// timeline (fracionário), `framesPerBucket` frames cada; valor u8 com
    /// compansão raiz (ver audio::WaveformCache). Devolve os baldes escritos,
    /// 0 = a camada não tem som. Baldes ainda em cálculo saem 0 e o status
    /// (`thumbnailGeneration`) muda quando chegam.
    u32 query_waveform(u64 layerId, f64 startFrame, f64 framesPerBucket, u32 count, u8* out) noexcept;

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
    // Export — o MESMO renderer do preview, quadro a quadro, no tempo de saída.
    //
    // `settings.height` é o LADO MENOR pedido (720/1080/1440/2160); a largura
    // sai da proporção da composição (nunca estica). `fps` 0 = o da composição.
    // Roda numa thread própria; o preview fica congelado até terminar.
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
    [[nodiscard]] History& history() noexcept { return history_; }
    [[nodiscard]] EffectRegistry& effects() noexcept { return effectRegistry_; }
    [[nodiscard]] GPUBackend* gpu() noexcept { return gpu_.get(); }
    [[nodiscard]] Renderer& renderer() noexcept { return renderer_; }
    [[nodiscard]] MediaManager& media() noexcept { return media_; }
    [[nodiscard]] PlaybackController& playback() noexcept { return playback_; }
    [[nodiscard]] audio::AudioEngine& audio() noexcept { return audio_; }
    [[nodiscard]] CommandQueue& commands() noexcept { return *commandQueue_; }

    /// Executa um comando direto, sem fila. Testes e recuperação de projeto.
    [[nodiscard]] Status apply_command(const Command& cmd, const char* stringData = nullptr) noexcept;

    void debug_feed_frame_stats(const FrameStats& stats) noexcept;

    [[nodiscard]] const std::string& cache_directory() const noexcept { return config_.cacheDirectory; }
    [[nodiscard]] const std::string& documents_directory() const noexcept { return config_.documentsDirectory; }

private:
    [[nodiscard]] Status apply_command_internal(const Command& cmd, const char* stringData,
                                                bool recordUndo) noexcept;
    /// Snapshot "antes" no histórico, se o comando altera a composição.
    void record_history_locked(CommandType type) noexcept;
    [[nodiscard]] static bool mutates_model(CommandType type) noexcept;
    /// Depois de desfazer/refazer: seleção sem layers mortas, fontes de mídia
    /// órfãs fechadas, playback no tamanho novo.
    void after_history_restore_locked() noexcept;
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
    History            history_{};
    EffectRegistry     effectRegistry_{};
    AdaptiveResolutionController* adaptive_ = nullptr;

    std::unique_ptr<GPUBackend> gpu_;
    Renderer           renderer_;
    MediaManager       media_;
    ThumbnailService   thumbs_;
    PlaybackController playback_;
    audio::AudioEngine audio_;
    std::unique_ptr<audio::WaveformCache> waveforms_;
    u32  audioRevision_ = 0;          ///< modelRevision_ do último snapshot de áudio
    const Composition* audioComp_ = nullptr;
    u64  audioGeneration_ = 0;        ///< geração do playback com que o som começou
    /// Liga/desliga o som conforme o playback (play, pausa, seek tocando,
    /// loop, velocidade ≠ 1). Sob o lock do modelo.
    void sync_audio_locked(const Composition& comp) noexcept;
    static std::string audio_path_resolver(void* self, const std::string& stored);
    FrameScheduler     frameScheduler_;
    FrameSnapshot      snapshot_;

    std::unique_ptr<CommandQueue> commandQueue_;
    std::unique_ptr<Project>      project_;
    std::unordered_map<u64, ImagePixels> images_;   ///< por AssetId empacotado
    /// Modelos 3D carregados, por AssetId. Um por asset, qualquer número de
    /// layers (ModelInstance) apontando para ele.
    std::unordered_map<u64, std::shared_ptr<const scene3d::SceneAsset>> models_;
    static std::shared_ptr<const scene3d::SceneAsset> model_lookup(void* self, AssetId id);
    [[nodiscard]] std::string resolve_asset_path(const std::string& stored) const;
    [[nodiscard]] std::string store_asset_path(const std::string& absolute) const;

    std::vector<u64> selection_;
    std::atomic<u32> modelRevision_{1};   ///< a UI relê listas quando muda

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
    // Ritmo do preview pelo conteúdo (render_frame(onlyIfChanged)).
    std::atomic<bool> forceRender_{true};     ///< a UI mudou algo / superfície nova
    std::atomic<u64>  mediaReadyGen_{0};      ///< frames novos do decoder
    i64  lastRenderedFrame_ = -1;
    u32  lastRenderedRevision_ = 0;           ///< modelRevision_ do último frame desenhado
    u32  incompleteRetries_ = 0;              ///< quadros seguidos com camada pendente
    u64  lastMediaGen_ = 0;
    bool lastIncomplete_ = true;              ///< último frame tinha vídeo faltando/aproximado
    bool lastSkipped_ = false;
    u64  nextFrameDueNs_ = 0;                 ///< quando o playhead muda de frame (tocando)
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
    /// Export em andamento: o render do preview não toca na GPU nem nos
    /// decoders (que o export usa em sequência).
    std::atomic<bool> exportActive_{false};
    void export_thread_main() noexcept;
    [[nodiscard]] Status render_export_frame(FrameIndex t, const OffscreenTarget& target) noexcept;
    [[nodiscard]] Status write_export_audio(i64 untilSample) noexcept;
};

} // namespace aurea
