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

#include "aurea/text/Captions.hpp"
#include "aurea/effects/CubeLut.hpp"
#include "aurea/project/AeImport.hpp"
#include "aurea/project/AlightMotion.hpp"
#include "aurea/project/Presets.hpp"
#include "aurea/project/ProjectPackage.hpp"
#include "aurea/export/ExportSink.hpp"
#include "aurea/export/ImageEncode.hpp"
#include "aurea/rig/AutoRig.hpp"
#include "aurea/scene3d/Importer.hpp"
#include "aurea/scene3d/ModelBudget.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/scene3d/Shape3D.hpp"
#include "aurea/text/FontManager.hpp"

#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/CommandQueue.hpp"
#include "aurea/command/History.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/jobs/JobSystem.hpp"
#include "aurea/media/ImageSequence.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/media/ThumbnailService.hpp"
#include "aurea/memory/MemoryManager.hpp"
#include "aurea/platform/DeviceCapabilities.hpp"
#include "aurea/playback/Playback.hpp"
#include "aurea/plugins/PluginManager.hpp"
// 0.3.4 plugins: runtime de script dos plugins (painéis, API de objetos).
#include "aurea/plugins/PluginRuntime.hpp"
#include "aurea/tracking/RealCameraCapture.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/GPUBackend.hpp"
#include "aurea/render/RenderScheduler.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/render/PreviewRefill.hpp"
#include "aurea/render/PreviewCachePolicy.hpp"
#include "aurea/render/PreRender.hpp"
#include "aurea/render/PreRenderCache.hpp"
#include "aurea/hyper/Hyper.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace aurea { class OfflineFrameRenderer; }
namespace aurea::export_checkpoint { struct SourceIdentity; }
namespace aurea {
class PreRenderCache;
/// Job Hyper: o projeto temporário e o grafo de um export (src/hyper/HyperExport.hpp).
struct HyperExportOverride;

/// Stable bridge values. Timing arrangements move whole layers without trimming.
enum class LayerTimeArrangement : u32 {
    AlignStarts = 0, Sequence = 1, AlignEnds = 2,
    DistributeStarts = 3, DistributeGaps = 4,
    StartsAtPlayhead = 5, EndsAtPlayhead = 6,
};

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
    // Preview AUTO 2.0 (Fase 8C §164): a escala e as reduções em vigor, e o gargalo medido.
    u32  previewDenominator = 1;
    u32  previewHeavyLevel = 0;
    PreviewBottleneck previewBottleneck = PreviewBottleneck::None;
    u32  previewUpBackoff = 1;
    u32  culledLayers = 0;          ///< camadas fora da tela no último quadro (sem decode/passe)
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
    /// Persist platform locators (e.g. Android gallery URI) for a frozen export.
    /// iOS/local files already resolve to persistent paths and need no adapter.
    /// 0.3.2 (CheckpointManager): identidade LIMITADA (tamanho, data, blocos
    /// amostrados — export_checkpoint::fd_identity) de uma fonte que a
    /// plataforma abre por URI (Android content://), pelo mesmo caminho que o
    /// export usa para abri-la. Sem isto, ou com erro, um projeto com essa fonte
    /// nunca retoma (recomeça do zero): não se continua com fonte não verificável.
    Status (*exportSourceIdentity)(const std::string& uri, export_checkpoint::SourceIdentity& out,
        const std::atomic<bool>* cancel) noexcept = nullptr;
    Result<std::string> (*exportAssetPathResolver)(const std::string&, const std::string&,
        const std::atomic<bool>*) noexcept = nullptr;
    /// Quadros de export em voo (GPU renderizando um enquanto o encoder recebe
    /// o anterior). 0 = automático (3; 1 sob calor). 1 = serial — o teste de
    /// equivalência compara os dois bytes a bytes.
    u32 exportPipelineDepth = 0;
    /// Hard limit for offline readback leases. Zero derives 8..64 MiB from
    /// the device budget; a single frame exceeding it fails before allocation.
    u64 exportFrameQueueBudgetBytes = 0;
    ExportExecutionProfile exportExecutionProfile = ExportExecutionProfile::Balanced;
    /// Staged bounded 3D exposure path, retaining the original route.
    bool enableBoundedSceneExposure = false;
    /// Admit transient targets against tracked GPU residency and known cache
    /// backing within the existing device budget, instead of a fixed half share.
    /// Other platforms retain the legacy policy until device validation.
    bool enableTrackedGpuAdmission = false;
    /// Rasterize only occupied glyph texels; retain the virtual canvas for effects.
    /// The switch exists for native pixel comparison and controlled rollback.
    bool enableBoundedTextSurface = true;
    /// Watchdog do worker do encoder (export/ExportWatchdog.hpp): sem batida
    /// da plataforma por isto, o motor desiste do worker travado e conclui o
    /// export com falha (EncoderStalled) em vez de esperar o join para sempre.
    /// 0 = adaptativo por fase (export_worker_hang_limit_ns: preparação 20 s,
    /// regime max(12 s, 8 × média por quadro), teto 45 s). Os testes usam menos.
    u32 exportWorkerHangMs = 0;
    /// 0.2.4 (C2): teto de sanidade da duração do export, em segundos.
    /// Acima disso o motor recusa (NotSupported, "duracao acima de 3 h").
    /// 0 = sem teto (testes de linha do tempo longa).
    f64 maxExportSeconds = 3.0 * 3600.0;
    /// Verificação pós-export ESTRITA (ExportOutputValidation::strict): toda
    /// divergência de PTS/duração/contagem/dimensão/áudio reprova o arquivo.
    /// Os apps ficam no tolerante (padrão): divergência vira aviso no
    /// diagnóstico e o arquivo é publicado; só reprova o que não abre, vazio,
    /// sem vídeo ou sem nenhum quadro decodificável. Os testes de host que
    /// guardam o export (retomável, registro de pacotes, PTS) ligam o estrito.
    bool strictExportVerification = false;
    /// Opt-in integrity recovery. A checked, frozen export snapshot is
    /// persisted before codec open; version 1 restarts from frame zero.
    bool enableExportRecovery = false;
    /// Export retomável (CheckpointManager, export/ExportCheckpoint.hpp): os
    /// pacotes de vídeo vão para um registro com checksum (ExportPacketLog) e o
    /// MP4 é montado, validado e publicado atomicamente no fim. Um start_export
    /// com o MESMO projeto, as mesmas configurações, as mesmas fontes e o mesmo
    /// encoder continua do último GOP íntegro (o app fechou, o processo morreu,
    /// o encoder falhou no meio, o iOS suspendeu); qualquer diferença recomeça
    /// do zero. Sem espaço para o registro, o export sai pelo MP4 direto (sem
    /// retomada; o diagnóstico diz). Upscale de IA (estado entre quadros) não
    /// usa registro. Desligado = MP4 direto sempre.
    bool enableResumableExport = false;
    /// NV12 do export com 4 bytes por fragmento (yuv_encode_packed.frag), 1/4
    /// dos fragmentos. Sem dither, os mesmos bytes; com dither, raros pixels
    /// diferem em 1 nível. DESLIGADO até ser medido no aparelho.
    bool enablePackedYuvExport = false;
    /// Export direto na Surface do encoder (Android, degrau surface-direct da
    /// escada): a composição sai em R'G'B' num AHardwareBuffer que a Surface
    /// recebe sem readback. DESLIGADO até ser validado em aparelho
    /// (docs/architecture/EXPORT_SURFACE_DIRETO_2026-10-15.md); no Android
    /// também liga com `adb shell setprop debug.aurea.enc.direct 1`.
    bool enableDirectSurfaceExport = false;
    /// Deadline for a submitted export/startup GPU fence. Zero = 120 seconds.
    u32 exportGpuTimeoutMs = 0;
    /// Quanto o export espera, sem avanço do decoder, o quadro EXATO da fonte
    /// antes de usar o mais próximo do mesmo trecho (OfflineFrameRenderer::
    /// ExactFrameDeadlines; a fonte é reaberta na metade). Zero = 16 s.
    u32 exportApproximateFrameMs = 0;

    /// Saída de som da plataforma (AAudio no Android). NÃO é assumida a posse.
    /// Nula = preview mudo; o relógio do sistema conduz o playback e o export
    /// continua mixando o áudio normalmente.
    audio::AudioOutput* audioOutput = nullptr;

    f32   displayRefreshRate = 60.0f;

    /// O que a camada de plataforma mediu do aparelho: núcleos grandes e
    /// pequenos, memória e a tabela de codecs do MediaCodecList.
    ///
    /// A plataforma mede UMA vez (a enumeração de codecs custa dezenas de ms) e
    /// guarda; depois passa aqui toda vez. Com `hasPlatformInfo` falso o motor
    /// detecta o que conseguir sozinho e fica no conservador no resto — nunca
    /// num valor otimista inventado.
    PlatformInfo platformInfo{};
    bool hasPlatformInfo = false;
    /// AureaHyper: a plataforma tem acelerador neural utilizável (Android:
    /// NNAPI com dispositivo acelerador; iOS: Core ML com Neural Engine). Só
    /// informa o medidor (hyper::DeviceProfile::npu); o motor não o usa sozinho.
    bool hyperNpuAvailable = false;
    /// Fonte padrão do texto (arquivo TTF/OTF); vazio = procurar a do sistema.
    std::string defaultFontPath;
    std::string cacheDirectory;
    // App-owned intermediate exports only. Empty when output files are final
    // user documents (iOS); never infer a cleanup directory from outputPath.
    std::string exportTemporaryDirectory;
    std::string documentsDirectory;
    /// Pasta dos plugins de efeito instalados (plugins/PluginManager.hpp):
    /// Android `files/plugins`, iOS `Documents/Plugins`. Vazio = sem plugins.
    /// Todo plugin da pasta é lido e registrado na inicialização.
    std::string pluginDirectory;
    u32   workerCount = 0;
    u64   memoryBudgetBytes = 0;
    /// Teto do orçamento medido (0 = sem teto). Android: HyperOS/MIUI mata o
    /// processo que passa de ~600 MB de memória de GPU ("GpuMemory OOM",
    /// relatório de 12/10/2026 no meio de um export); o orçamento de ¼ da RAM
    /// livre chegava a 1 GB num aparelho de 8 GB.
    u64   memoryBudgetCapBytes = 0;
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
    /// "Otimizar modelo" (ModelBudget.hpp): a qualidade escolhida fica no asset
    /// e vale de novo ao reabrir o projeto.
    scene3d::ModelQuality quality = scene3d::ModelQuality::Original;
    /// Memória medida pela plataforma na hora do import (zeros = a do início).
    scene3d::DeviceMemoryHint memory{};
};

/// O que o último import de modelo fez (a UI mostra "otimizado: A → B").
struct ModelImportReport {
    u32 sourceTriangles = 0;
    u32 triangles = 0;
    u32 texturesReduced = 0;
    u32 texturesSkipped = 0;
    scene3d::ModelQuality quality = scene3d::ModelQuality::Original;
};

/// Pedido de importação de vídeo.
struct VideoImport {
    std::string sourcePath;    ///< caminho, ou "fd:<n>" no Android
    std::string displayName;
};

/// Pedido de importação de SEQUÊNCIA DE IMAGENS (ImageSequence.hpp). Os
/// caminhos são arquivos locais (o Android copia o que vem de `content://`
/// para o armazenamento do app antes de chamar).
struct ImageSequenceImport {
    /// Os quadros (qualquer ordem); OU um quadro só (o motor acha os irmãos na
    /// pasta); OU uma pasta; OU um .zip de imagens (extraído em
    /// Documentos/Media/sequences/<hash>/ — inclusive o .zip da sequência PNG
    /// que o próprio app exporta).
    std::vector<std::string> paths;
    f64 fps = 0.0;                                         ///< 0 = o sugerido pela sondagem
    SequenceAlphaMode alpha = SequenceAlphaMode::Auto;     ///< Auto: PNG/TGA reto, EXR pré-multiplicado, sem canal = opaco
    SequenceColorMode color = SequenceColorMode::Auto;     ///< Auto: EXR linear, o resto sRGB
    std::string displayName;                               ///< vazio = o padrão ("render_####.png")
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

    /// Pressão de memória do SISTEMA (Fase 8 §13): o nível do Android
    /// (ComponentCallbacks2.TRIM_MEMORY_*: 5, 10, 15, 20, 40, 60, 80) vira um
    /// estágio da ordem de despejo — miniaturas fora da tela, waveform antiga,
    /// quadros sem uso, cache de render antigo, mips altos, assets 3D sem uso,
    /// temporários. Projeto, alterações não salvas, histórico e timeline nunca
    /// entram. Qualquer thread. Devolve o relatório (bytes por estágio; os de
    /// GPU medidos no backend).
    MemoryManager::TrimReport trim_memory(i32 osLevel) noexcept;
    /// Classe de memória LOW (RAM total até ~4 GB): prévia guardada até 32 MiB,
    /// sombra do preview até 2048, cache do Roto Brush pela metade.
    [[nodiscard]] bool low_memory_device() const noexcept;
    /// Um aviso RUNNING_LOW (ou acima) chegou há menos de
    /// kPreviewPressureHoldNs: prévia guardada desligada e preparo ocioso parado.
    [[nodiscard]] bool preview_memory_hold() const noexcept;

    // --- Superfície (Android: SurfaceView → ANativeWindow) -------------------
    /// Chamadas da thread da UI. `detach_surface` só volta depois que a GPU
    /// parou de usar a janela — o Android destrói a superfície logo depois.
    /// Durante o export, se a GPU estiver presa no start (driver compilando),
    /// anexar/soltar NÃO espera: a operação fica pendente e a thread de render
    /// a aplica quando o export larga a GPU (vivo 1938, ANR no 2185). O
    /// VkSurfaceKHR/EGLSurface segura a própria referência da janela, então o
    /// chamador pode soltar a dele já.
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
    /// Só acorda: a thread redesenha se algo mudou (playhead, modelo, frame de
    /// vídeo que faltava) e dorme de novo se não. É o pulso do vsync no iOS —
    /// `request_render` ali forçava um quadro inteiro a cada vsync, mesmo parado.
    void wake_render() noexcept;
    /// Quantas vezes a thread de render acordou desde que subiu (§38: parado,
    /// sem superfície, tem de ficar parado; com superfície, no máximo a rede
    /// de 500 ms).
    [[nodiscard]] u64 render_wakeups() const noexcept { return renderWakeups_.load(std::memory_order_relaxed); }
    /// Acordadas dos workers do pool (ver JobSystem::idle_wakeups).
    [[nodiscard]] u64 job_idle_wakeups() const noexcept { return jobs_.idle_wakeups(); }
    /// Redesenha e reapresenta mesmo sem mudança no modelo: a janela voltou a
    /// aparecer (seletor do sistema fechou) e o último quadro apresentado com
    /// ela escondida pode ter sido descartado pelo compositor.
    void invalidate() noexcept;

    // =========================================================================
    // Projeto
    // =========================================================================
    /// `backgroundRgba` (opcional, 4 floats sRGB alfa reto): a cor de fundo
    /// com que a composição principal nasce — fora do histórico (não é uma
    /// edição a desfazer). Nulo = preto, como sempre.
    [[nodiscard]] Status new_project(u32 width = 1920, u32 height = 1080,
                                     f64 fps = 30.0, const char* title = nullptr,
                                     const f32* backgroundRgba = nullptr) noexcept;
    /// Abre o projeto. Principal ilegível (truncado, CRC, lixo) → tenta o
    /// `.tmp` (gravação interrompida antes do rename) e o `.bak` (versão
    /// anterior) e guarda o principal ruim em `.corrompido`; nada válido →
    /// ProjectCorrupted, e o projeto aberto antes continua aberto. Versão
    /// futura → UnsupportedVersion, sem tentar cópias (abrir uma mais velha e
    /// salvar por cima perderia o trabalho). O que foi preciso fazer fica em
    /// `last_load_notice()`.
    [[nodiscard]] Status load_project(const char* path) noexcept;
    /// Grava: copia o modelo sob o lock (encode) e faz a E/S com fsync FORA
    /// dele. Uma gravação por vez; o "sujo" só é limpo se nada mudou durante
    /// a escrita. Disco cheio → StorageFull, com o arquivo anterior intacto.
    [[nodiscard]] Status save_project(const char* path) noexcept;
    [[nodiscard]] Status save_project() noexcept;
    [[nodiscard]] Status autosave_project() noexcept;
    /// Sair do app / ir para segundo plano: drena a fila e grava se houver
    /// QUALQUER mudança — sem as condições do autosave (tocando, gesto aberto),
    /// porque depois disso o processo pode morrer. Limpo = OkStatus sem tocar
    /// no disco (o `.bak` continua sendo a gravação anterior de verdade).
    /// `saved` diz se escreveu (a plataforma refaz a capa só nesse caso).
    [[nodiscard]] Status save_project_if_dirty(bool* saved = nullptr) noexcept;

    /// Bits do que a última abertura precisou fazer (0 = abriu limpo). A UI
    /// avisa em vez de esconder (§55, §120, §124).
    enum LoadNotice : u32 {
        kLoadRecoveredCopy = 1u << 0,   ///< principal ilegível: abriu o .tmp ou o .bak
        kLoadPartial       = 1u << 1,   ///< abriu com seções faltando (a timeline veio)
        kLoadOlderFormat   = 1u << 2,   ///< formato antigo: cópia guardada antes de regravar
        kLoadMissingMedia  = 1u << 3,   ///< imagem/fonte/modelo 3D ausente: fica o espaço, religar
    };
    [[nodiscard]] u32 last_load_notice() const noexcept { return lastLoadNotice_.load(std::memory_order_relaxed); }
    /// Assets que a última abertura não conseguiu ler (imagens + modelos + fontes).
    [[nodiscard]] u32 last_load_missing_assets() const noexcept { return lastLoadMissing_.load(std::memory_order_relaxed); }

    /// Medidas das gravações (diagnóstico §2/§57): o tempo com o lock do modelo
    /// preso é o que a UI pode sentir; o da escrita não trava ninguém.
    struct SaveStats {
        u64  lastLockNs = 0;     ///< encode sob o lock do modelo
        u64  lastWriteNs = 0;    ///< escrita + fsync + rename, sem lock
        u64  maxLockNs = 0;
        u64  lastBytes = 0;
        u32  saves = 0;
        u32  failures = 0;
        Errc lastError = Errc::Ok;
    };
    [[nodiscard]] SaveStats save_stats() const noexcept;
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
    /// Sequência de imagens (EngineImageSequence.cpp): acha o padrão
    /// (prefixo, dígitos, sufixo), a numeração, os quadros ausentes, tamanho,
    /// alfa, profundidade e o fps sugerido. Não muda o projeto. Um .zip é
    /// extraído aqui (a importação seguinte reaproveita a pasta).
    [[nodiscard]] Result<ImageSequenceProbe> probe_image_sequence(const std::vector<std::string>& paths) noexcept;
    /// Cria o asset (AssetKind::Video com a receita "aurea-seq:") e a camada,
    /// do mesmo jeito que `import_video` (no topo, no cabeçote; no modo Edição,
    /// no fim da linha magnética). Duração = quadros / fps. Devolve o id da camada.
    [[nodiscard]] Result<u64> import_image_sequence(const ImageSequenceImport& request) noexcept;
    /// "Substituir mídia" (EngineMedia.cpp): troca a fonte da camada de vídeo
    /// ou imagem por outro vídeo/foto, mantendo transform, keyframes, efeitos,
    /// máscaras e tempo. A mídia nova ocupa a MESMA caixa na tela (cabe dentro
    /// da antiga; âncora e máscaras acompanham na proporção). Vídeo novo mais
    /// curto que o trecho encurta o fim. Um passo de desfazer. Devolve o id.
    [[nodiscard]] Result<u64> replace_layer_video(u64 layerId, const VideoImport& request) noexcept;
    [[nodiscard]] Result<u64> replace_layer_image(u64 layerId, const u8* rgba, u32 width, u32 height,
                                                  const char* name, const char* sourcePath, u8 orientation = 1) noexcept;
    /// Arquivo de origem da camada de vídeo, áudio ou imagem (caminho legível
    /// neste aparelho ou URI). Vazio = sem arquivo. É o "Informações da mídia".
    [[nodiscard]] std::string layer_source_path(u64 layerId) noexcept;
    /// Mídias de um `.aurea` fechado, já com o caminho legível (`resolved`).
    /// É o "Exportar arquivo do projeto" (ProjectPackage.hpp).
    [[nodiscard]] Status project_file_media(const char* path, std::vector<package::MediaRef>& out) noexcept;

    // --- Omega adicionar (área 08) — src/engine/EngineOmegaAdd.cpp -----------------
    /// Como o lote do navegador de mídia arruma as camadas (número estável das pontes).
    enum class MediaBatchPlacement : u32 {
        SameStart = 0,   ///< todas no cabeçote; a 1ª escolhida embaixo, as seguintes acima
        Sequence = 1,    ///< uma depois da outra, na ordem de escolha, a partir do cabeçote
    };
    /// Abre um LOTE de importação: tudo o que entrar até `end_import_batch`
    /// (vídeos, imagens, cortes e durações) vira UM passo de desfazer. A cópia e
    /// a decodificação continuam na plataforma (uma mídia por vez, memória
    /// limitada); o motor só guarda o passo aberto. Abrir de novo não empilha.
    void begin_import_batch() noexcept;
    /// Fecha o lote: arruma `ids` (na ordem de escolha) pelo `placement` a partir
    /// do cabeçote de quando o lote abriu, escolhe as camadas e fecha o passo.
    /// Devolve quantas camadas andaram no tempo. Sem lote aberto, só arruma.
    [[nodiscard]] Result<u32> end_import_batch(const u64* ids, u32 count, MediaBatchPlacement placement) noexcept;
    /// Duração e corte pedidos no navegador, aplicados à camada RECÉM-importada
    /// no MESMO passo de desfazer da importação (dentro ou fora do lote):
    /// imagem: `durationFrames` > 0 = a camada dura isso (quadros da composição);
    /// vídeo: `sourceOutUs` > `sourceInUs` ≥ 0 = a camada mostra só esse trecho
    /// da fonte (começa no cabeçote; comprimento = out − in; a origem desloca).
    /// Zeros = a regra de sempre. A composição cresce quando a camada passa do fim.
    [[nodiscard]] Status apply_import_timing(u64 layerId, i64 durationFrames, i64 sourceInUs, i64 sourceOutUs) noexcept;
    /// Um item do lote já pronto para o motor (vídeo por caminho; imagem já
    /// decodificada em RGBA8, como `import_image`).
    struct MediaBatchItem {
        bool video = false;
        VideoImport source;          ///< vídeo: caminho e nome
        i64 sourceInUs = 0;          ///< vídeo: corte (0, 0 = inteiro)
        i64 sourceOutUs = 0;
        const u8* rgba = nullptr;    ///< imagem: pixels (não é copiado depois da chamada)
        u32 width = 0;
        u32 height = 0;
        std::string name;
        std::string sourcePath;
        u8 orientation = 1;
        i64 durationFrames = 0;      ///< imagem: duração (0 = a regra de sempre)
    };
    /// O lote inteiro numa chamada (testes e quem já tem tudo em memória): abre,
    /// importa na ordem, aplica corte e duração, arruma e fecha — UM desfazer.
    /// Devolve os ids que entraram (na ordem); `firstError` = a 1ª recusa.
    [[nodiscard]] std::vector<u64> import_media_batch(const std::vector<MediaBatchItem>& items,
                                                      MediaBatchPlacement placement, Status* firstError = nullptr) noexcept;
    /// "Grupo vazio" (P1-5): uma composição nova e vazia (tamanho, fps e
    /// duração da atual; fundo transparente) e UMA camada que a mostra, no
    /// cabeçote, já escolhida. Um passo de desfazer. `name` nulo = "Grupo vazio".
    [[nodiscard]] Result<u64> add_empty_group(const char* name = nullptr) noexcept;
    /// "Elemento/Projeto" (P1-6): outro projeto `.aurea` vira UMA camada de
    /// grupo no cabeçote — a composição principal dele e as aninhadas entram
    /// com handles novos e a mídia é importada (o mesmo caminho do colar entre
    /// projetos). Recusa o próprio projeto (ciclo). Um passo de desfazer.
    [[nodiscard]] Result<u64> insert_project_as_layer(const char* projectPath, const char* name = nullptr) noexcept;
private:
    bool omegaImportBatch_ = false;   ///< Omega adicionar: lote aberto (begin/end_import_batch)
    i64 omegaImportBatchFrame_ = 0;   ///< o cabeçote quando o lote abriu
public:
    /// "Extrair o áudio": o som de um vídeo vira uma camada própria (mesmo
    /// tempo, mesmo corte) e o vídeo fica mudo. Uma ação de desfazer.
    [[nodiscard]] Result<u64> extract_audio(u64 videoLayerId) noexcept;
    /// "Congelar quadro": divide o clipe no `frame`, insere ali `holdFrames`
    /// do quadro parado e empurra o resto do clipe para depois. Uma ação de
    /// desfazer. Devolve o id do clipe congelado.
    [[nodiscard]] Result<u64> freeze_frame(u64 layerId, i64 frame, i64 holdFrames) noexcept;
    /// Nova forma no centro da composição, do cabeçote até o fim. `preset` é
    /// o ladrilho da aba Forma (0..32; a tabela em Engine::add_shape). Devolve
    /// o id da camada.
    [[nodiscard]] Result<u64> add_shape(u32 preset) noexcept;

    // --- Camada vetorial (Fase 7D; EngineVector.cpp) --------------------------------
    // Coordenadas: a camada nasce do tamanho da composição, com a âncora no
    // centro e a posição no centro — espaço da camada = espaço da composição
    // até alguém mover/girar a camada. Os caminhos ficam no espaço do GRUPO.
    // `continuing` = continuação de um arrasto: não abre passo de desfazer
    // novo (o primeiro envio do gesto, com false, capturou o "antes").
    /// Nova camada vetorial: 0 vazia (um caminho livre sem pontos, contorno
    /// branco — pronta para o modo de pontos), 1 retângulo, 2 elipse,
    /// 3 polígono, 4 estrela. Do cabeçote até o fim.
    [[nodiscard]] Result<u64> add_vector_layer(u32 preset) noexcept;
    /// Documento inteiro (vector::encode_document) e nomes dos grupos.
    bool vector_document(u64 layerId, std::vector<f32>& out, std::string& names) noexcept;
    /// Substitui o documento. Mesmo nº de grupos (a estrutura muda pelas
    /// funções abaixo, que remapeiam as trilhas animadas).
    bool set_vector_document(u64 layerId, const f32* data, usize count, const std::string& names, bool continuing) noexcept;
    /// Caminho avaliado no cabeçote (morph e paramétrico resolvidos), no espaço
    /// do grupo, precedido da afim grupo → composição (a b c d tx ty) e de
    /// flags (bit0 livre, bit1 tem keyframes de forma, bit2 keyframe no cabeçote).
    bool vector_path_at(u64 layerId, u32 group, u32 path, std::vector<f32>& out) noexcept;
    /// Grava a forma do caminho livre (bezier codificado). Com keyframes de
    /// forma, grava no cabeçote (cria o keyframe se não houver).
    bool set_vector_path(u64 layerId, u32 group, u32 path, const f32* bez, usize count, bool continuing) noexcept;
    /// Liga/desliga o keyframe de forma no cabeçote (morph).
    bool toggle_vector_path_key(u64 layerId, u32 group, u32 path) noexcept;
    /// Marca o keyframe do caminho aqui — com keyframe no cabeçote, REGRAVA o
    /// valor avaliado em vez de apagá-lo (ver `ensure_shape_param_key`).
    bool ensure_vector_path_key(u64 layerId, u32 group, u32 path) noexcept;
    /// Grupo novo com um caminho (VectorPathKind; Free = sem pontos). Índice ou −1.
    i32 add_vector_group(u64 layerId, u32 pathKind) noexcept;
    bool remove_vector_group(u64 layerId, u32 group) noexcept;
    /// Caminho novo no grupo (bezier opcional para Free). Índice ou −1.
    i32 add_vector_path(u64 layerId, u32 group, u32 pathKind, const f32* bez, usize count) noexcept;
    bool remove_vector_path(u64 layerId, u32 group, u32 path) noexcept;
    /// Paramétrico → caminho livre editável (mesma forma).
    bool make_vector_path_editable(u64 layerId, u32 group, u32 path) noexcept;
    /// Valores animáveis do grupo no cabeçote (kVecParamCount floats) + bits
    /// animados + bits com keyframe no cabeçote. Devolve quantos floats.
    static constexpr u32 kVectorParamFloats = kVecParamCount + 2;
    u32 query_vector_params(u64 layerId, u32 group, f32* out, u32 capacity) noexcept;
    /// Valor (VectorParam): com keyframes, grava no cabeçote.
    bool set_vector_param(u64 layerId, u32 group, u32 param, f32 value, bool continuing) noexcept;
    bool toggle_vector_param_key(u64 layerId, u32 group, u32 param) noexcept;
    /// Marca o keyframe do parâmetro aqui — com keyframe no cabeçote, REGRAVA o
    /// valor avaliado em vez de apagá-lo (ver `ensure_shape_param_key`).
    bool ensure_vector_param_key(u64 layerId, u32 group, u32 param) noexcept;

    /// Forma (SDF): os mesmos parâmetros de `ShapeSetParam` (1 raio, 2 lados,
    /// 3 raio interno, 4 contorno, 5 largura, 6 altura, 7 profundidade,
    /// 8 ponta, 9 espessura, 10 abertura, 11 ponta da seta, 12 haste,
    /// 13 amplitude, 14 variante — shape::Param em timeline/ShapeGeometry.hpp),
    /// todos ANIMÁVEIS. `out` recebe os kShapeParamCount valores no playhead
    /// (o 0 é o tipo; 7..14 já com o padrão da forma resolvido) + bits de
    /// animado + bits de keyframe aqui (kShapeParamFloats no total).
    static constexpr u32 kShapeParamCount = 15;
    static constexpr u32 kShapeParamFloats = kShapeParamCount + 2;
    u32 query_shape_params(u64 layerId, f32* out, u32 capacity) noexcept;
    bool set_shape_param(u64 layerId, u32 param, f32 value, bool continuing) noexcept;
    bool toggle_shape_param_key(u64 layerId, u32 param) noexcept;
    /// Marca o keyframe do parâmetro aqui — e, se já houver um, REGRAVA com o
    /// valor avaliado no cabeçote em vez de apagá-lo.
    ///
    /// É o que o losango do painel usa: depois de editar um valor num quadro que
    /// já tinha keyframe, "criar keyframe" apagava justamente o valor recém
    /// editado e a forma voltava para o estado anterior. Quem quer tirar o
    /// keyframe usa o menu do keyframe na timeline (que apaga de verdade).
    bool ensure_shape_param_key(u64 layerId, u32 param) noexcept;
    /// Desenho à mão livre: pontos do dedo (x,y em px da composição) viram um
    /// caminho suave (ajuste de Schneider, `error` px). `layerId` = 0 cria uma
    /// camada vetorial nova; senão entra como grupo novo nela. Devolve a camada.
    [[nodiscard]] Result<u64> add_freehand_path(u64 layerId, const f32* xy, usize count, f32 error) noexcept;

    // --- Bugs 0.3.4 (bug 11; EngineOmegaOutline.cpp) -----------------------------
    /// "Converter em contorno": a forma VIVA (SDF) vira camada vetorial com a
    /// mesma silhueta (no cabeçote), preenchimento e contorno, editável ponto a
    /// ponto. Mantém id, transform, tempo, efeitos e máscaras; tira as trilhas
    /// da forma. Falso: não é forma viva, está travada ou não há silhueta.
    /// Um passo de desfazer.
    bool convert_shape_to_outline(u64 layerId) noexcept;

    // --- Omega conteúdo (área 06; EngineOmegaContent.cpp) -------------------------
    /// Camada de DESENHO vazia (vetorial com `VectorData::drawing`), do cabeçote
    /// até o fim, no centro. Um passo de desfazer. Devolve o id.
    [[nodiscard]] Result<u64> add_drawing_layer() noexcept;
    /// Ferramentas do desenho (add_drawing_stroke).
    static constexpr u32 kDrawPen = 0, kDrawBrush = 1, kDrawFill = 2, kDrawEraser = 3;
    /// Um traço do editor de desenho vira um grupo novo na camada (`layerId` 0
    /// cria a camada de desenho). `xyp` = x, y (px da COMPOSIÇÃO, como a mão
    /// livre) e pressão 0..1, intercalados (`count` floats, múltiplo de 3).
    /// `rgba` = cor sRGB com alfa reto; `width` = espessura em px da composição.
    ///  - caneta: curva suave (Schneider) com contorno redondo de largura fixa;
    ///  - pincel: o mesmo, com a largura pela pressão (VectorStroke::widthProfile);
    ///  - preencher: caminho fechado pelos pontos, só preenchimento;
    ///  - borracha: contorno redondo com composite = apagar (tira dos de baixo).
    /// Um passo de desfazer por traço. Devolve a camada.
    [[nodiscard]] Result<u64> add_drawing_stroke(u64 layerId, u32 tool, const f32* rgba, f32 width, const f32* xyp, usize count) noexcept;
    /// Bits da camada vetorial: 1 é vetorial, 2 é desenho. 0 = não é vetorial.
    [[nodiscard]] u32 vector_layer_flags(u64 layerId) noexcept;
    /// Operações de ponto (vector/PathEdit.hpp) no caminho livre `path` do
    /// grupo, no espaço do GRUPO. Com keyframes de forma, grava o keyframe do
    /// cabeçote (como `set_vector_path`). `continuing` = mesmo arrasto.
    /// Devolve o índice do ponto que fica escolhido (ou 0), −1 = falhou.
    enum class PathOp : u32 {
        MovePoints = 0,   ///< dx, dy, i0, i1, …          (laço: vários pontos)
        MoveHandle = 1,   ///< i, lado (0 in, 1 out), dx, dy, travaÂngulo, travaComprimento
        Insert = 2,       ///< trecho, t                   → índice do ponto novo
        Remove = 3,       ///< i                           → o vizinho que fica escolhido (−1 vazio)
        SetType = 4,      ///< i, curva (0 canto, 1 curva)
        Append = 5,       ///< x, y, noComeço (0/1)        → índice do ponto novo
        SetClosed = 6,    ///< fechado (0/1)
        SetExclude = 7,   ///< furo (0/1) — o contorno recorta o preenchimento do grupo
    };
    i32 edit_vector_path(u64 layerId, u32 group, u32 path, PathOp op, const f32* args, u32 argc, bool continuing) noexcept;
    /// Arrastar a alça de LARGURA da caixa de texto no palco (modo de texto).
    /// `dx, dy` em px da composição; o motor projeta no eixo X da camada. Pela
    /// direção do texto: esquerda + dx, centro + 2·dx, direita − dx; mínimo 50 px,
    /// arredondado. Texto de ponto (boxMode 0) vira parágrafo (1) com a largura
    /// atual. A borda oposta fica parada (a posição compensa, sem posição animada).
    bool drag_text_box_width(u64 layerId, f32 dx, f32 dy, bool continuing) noexcept;
    /// Alça de tamanho da forma (Editar forma): 0 TL, 1 TR, 2 BR, 3 BL, 4 cima,
    /// 5 direita, 6 baixo, 7 esquerda. `du, dv` = quanto o dedo andou DESDE o
    /// último envio, em px da forma (eixos da camada). `keepRatio` mantém a
    /// proporção; `fromCenter` muda em volta do centro (senão a borda oposta
    /// fica parada e a posição da camada anda junto). Respeita os keyframes.
    bool resize_shape_handle(u64 layerId, u32 handle, f32 du, f32 dv, bool keepRatio, bool fromCenter, bool continuing) noexcept;
    /// Apaga uma fonte IMPORTADA (tira da lista e apaga o arquivo da pasta das
    /// importadas). Os textos que a usavam voltam para a fonte padrão.
    bool remove_imported_font(const std::string& path) noexcept;

    /// SVG → camada vetorial nova (um grupo por forma), centrada na composição.
    [[nodiscard]] Result<u64> import_svg(const std::string& text, const char* name) noexcept;
    [[nodiscard]] Result<u64> import_psd(const std::string& path, const char* name, bool* approximated = nullptr) noexcept;
    /// GIF animado (0.2.7): uma pré-composição com um quadro por camada de
    /// imagem, cada um com a sua duração, tocando em loop até o fim da
    /// composição. GIF de um quadro só vira imagem do mesmo jeito.
    [[nodiscard]] Result<u64> import_gif(const std::string& path, const char* name) noexcept;
    [[nodiscard]] Result<u64> create_grid(const u64* ids, u32 count) noexcept;
    // Omega grid (0.3.5): a grade "controlada por" um nulo. A camada
    // controladora da grade vira filha do nulo (com a compensação do vincular:
    // nada pula na tela) e dali em diante a grade inteira segue a posição, a
    // rotação X/Y/Z, a escala e a âncora do nulo — e de toda a cadeia de pais
    // dele, sem teto de profundidade — no preview e no export.
    /// A camada é a controladora de uma grade?
    [[nodiscard]] bool is_grid_controller(u64 layerId) noexcept;
    /// O nulo que controla a grade (0 = nenhum).
    [[nodiscard]] u64 grid_control_null(u64 gridId) noexcept;
    /// Vincula a grade ao nulo (`nullId` 0 = desvincular). Um passo de
    /// desfazer. Devolve `grid::kLink*` (0 = ok; 3 = fecharia um ciclo).
    i32 set_grid_control_null(u64 gridId, u64 nullId) noexcept;
    /// "Criar nulo de controle": um nulo 3D no centro da grade (o centro dos
    /// elementos no mundo), já vinculado. Um passo de desfazer. `name` = o
    /// nome da camada nova (a UI manda o texto no idioma dela).
    [[nodiscard]] Result<u64> create_grid_control_null(u64 gridId, const char* name) noexcept;
    [[nodiscard]] std::string foreground_model_directory() const { return config_.documentsDirectory + "/ai/rotobrush"; }
    /// Texto no caminho: camada-guia vetorial (0 = desliga), margem inicial,
    /// perpendicular, invertido.
    bool set_text_path(u64 layerId, u64 pathLayer, f32 offset, bool perpendicular, bool reverse) noexcept;
    bool query_text_path(u64 layerId, u64& pathLayer, f32& offset, bool& perpendicular, bool& reverse) noexcept;
    /// Texto no caminho completo (as Path Options do After Effects, 0.2.7).
    /// `mask` = id de uma máscara da PRÓPRIA camada como caminho (vale mais
    /// que `pathLayer`; kInvalidIndex = nenhuma). As margens são animáveis
    /// (TrackProperty::TextPathFirstMargin/LastMargin): com keyframes na
    /// trilha, mudar a margem grava o keyframe do instante; a consulta devolve
    /// o valor no cabeçote.
    struct TextPathOptions {
        u64  pathLayer = 0;
        u32  mask = kInvalidIndex;
        f32  firstMargin = 0.0f;
        f32  lastMargin = 0.0f;
        bool perpendicular = true;
        bool reverse = false;
        bool forceAlign = false;
    };
    bool set_text_path_options(u64 layerId, const TextPathOptions& options) noexcept;
    bool query_text_path_options(u64 layerId, TextPathOptions& options) noexcept;
    // --- Fontes ---------------------------------------------------------------------
    /// Fontes disponíveis (aparelho + importadas), por família e peso.
    [[nodiscard]] std::vector<text::FontEntry> list_fonts() noexcept;
    /// Registra um TTF/OTF já copiado para a pasta do app. Nulo se não for fonte.
    [[nodiscard]] Result<text::FontEntry> import_font(const char* path) noexcept;
    [[nodiscard]] Status import_color_lut(u64 layer, u32 effect, const char* path) noexcept;
    [[nodiscard]] std::string color_lut_name(u64 layer, u32 effect) const noexcept;
    /// "Trocar textura 3D": a imagem `path` (já copiada para a pasta do app;
    /// decodificada pelo `imageLoader` da plataforma) vira o parâmetro Imagem
    /// do efeito `effect` da camada. Um passo de desfazer; o projeto guarda o
    /// caminho e a imagem volta ao reabrir, como as camadas de imagem.
    [[nodiscard]] Status set_effect_image(u64 layer, u32 effect, const char* path) noexcept;
    /// Nome do arquivo da imagem do efeito (vazio = nenhuma).
    [[nodiscard]] std::string effect_image_name(u64 layer, u32 effect) const noexcept;
    /// As PARTES da camada 3D na ordem dos efeitos de parte (Parte 1, 2...):
    /// o nome de cada uma (vazio = o arquivo não deu nome). Vazio = não é 3D.
    [[nodiscard]] std::vector<std::string> model_part_names(u64 layer) noexcept;
    /// Câmeras do arquivo do modelo (glTF/FBX) — 0.1.8. A importação já as põe
    /// na timeline (camadas de câmera filhas do modelo, com a animação do
    /// arquivo em keyframes); `add_model_cameras` põe de novo (um passo de
    /// desfazer) e devolve quantas entraram.
    [[nodiscard]] u32 model_camera_count(u64 layer) noexcept;
    // --- Pose do rig 3D (0.1.8) — scene3d/RigPose.hpp ----------------------
    /// Ossos posáveis do modelo no cabeçote, kBoneFloats por osso: [0] nó,
    /// [1] posição (na lista) do osso pai ou −1, [2..4] rotação da pose X/Y/Z °,
    /// [5] canais com keyframe (bits), [6] key no cabeçote (0/1), [7..8] posição
    /// na tela (px da composição, a vista da cena 3D quando ligada), [9] na
    /// frente da câmera. Devolve os floats necessários (capacity 0 = medir).
    static constexpr u32 kBoneFloats = 10;
    u32 query_model_bones(u64 layerId, f32* out, u32 capacity) noexcept;
    /// Dedo na cena: gira o osso como uma trackball (dx, dy em px da
    /// composição; twist em graus em volta do olhar). Osso com keyframe grava
    /// keyframe no cabeçote; sem, muda a pose parada. `continuing` = mesmo gesto.
    bool drag_bone(u64 layerId, u32 node, f32 dxPx, f32 dyPx, f32 twistDeg, bool continuing) noexcept;
    /// Marca/tira keyframe do osso no cabeçote (node < 0 = todos os ossos, como
    /// o "inserir keyframe" do Blender). 1 marcou, 0 tirou, −1 fora da camada.
    i32 toggle_bone_key(u64 layerId, i32 node) noexcept;
    /// Volta o osso (node < 0 = todos) à pose do arquivo: apaga as trilhas.
    bool reset_bone_pose(u64 layerId, i32 node) noexcept;
    // --- AutoRig 3D / esqueleto humanoide (0.3.6 BETA) — rig/Humanoid.hpp ---
    /// 0 sem esqueleto · 1 esqueleto do arquivo não humanoide · 2 rig
    /// compatível (importado, mapeado por nome) · 3 AutoRig do Aurea.
    [[nodiscard]] i32 humanoid_status(u64 layerId) noexcept;
    /// Mapa + repouso do rig humanoide da camada (false = nenhum usável).
    bool query_humanoid(u64 layerId, rig::HumanoidRig& out) noexcept;
    /// AutoRig assíncrono (JobSystem): detecta o corpo em T-pose, cria os 17
    /// ossos, a skin e os pesos. Ao terminar, a camada passa a usar o modelo
    /// com rig (um passo de desfazer). Falha com motivo em autorig_progress.
    bool autorig_start(u64 layerId) noexcept;
    struct AutoRigProgress {
        i32 state = 0;          ///< 0 parado, 1 processando, 2 pronto, 3 falhou, 4 cancelado
        f32 progress = 0.0f;    ///< 0..1
        i32 error = 0;          ///< rig::AutoRigError
        u64 layer = 0;
    };
    [[nodiscard]] AutoRigProgress autorig_progress() noexcept;
    void autorig_cancel() noexcept;
    /// Juntas canônicas na tela (Corrigir esqueleto), kRigJointFloats por osso
    /// canônico (17): [0] nó (−1 ausente), [1..2] tela (px da composição),
    /// [3] na frente da câmera, [4..6] posição de repouso no modelo, [7] pai canônico.
    static constexpr u32 kHumanoidJointFloats = 8;
    u32 query_humanoid_joints(u64 layerId, f32* out, u32 capacity) noexcept;
    /// AutoRig: move a junta para o ponto da tela (plano da vista pela junta) e
    /// recalcula os pesos (assíncrono, autorig_progress). Um passo de desfazer.
    bool humanoid_move_joint(u64 layerId, u32 bone, f32 compX, f32 compY) noexcept;
    /// Rig importado: reatribui o osso canônico ao nó (−1 = sem). Desfazer.
    bool humanoid_assign_bone(u64 layerId, u32 bone, i32 node) noexcept;
    /// Nome do nó do modelo (para a lista de ossos do rig importado).
    [[nodiscard]] std::string model_node_name(u64 layerId, i32 node) noexcept;
    /// Grava keys de osso em lote (mocap): UMA ação de desfazer. Com
    /// clearFrom ≤ clearTo, apaga antes as keys dos mesmos nós no intervalo
    /// (quadros locais). Rotações nos params 0..2; translação (3..5) opcional.
    Status set_bone_keys(u64 layerId, const rig::BoneKey* keys, usize count, const char* undoLabel,
                         i64 clearFrom = 0, i64 clearTo = -1) noexcept;
    /// Dedo na cena com o quadril em "mover": desloca o osso no plano da vista
    /// (params 3..5). Mesma regra de key do drag_bone.
    bool drag_bone_translate(u64 layerId, u32 node, f32 dxPx, f32 dyPx, bool continuing) noexcept;
    /// IK de dois ossos (limb 0 braço esq., 1 braço dir., 2 perna esq., 3 perna
    /// dir.). Ligar põe o alvo na mão/pé de agora e o polo à frente do
    /// cotovelo / joelho; desligar apaga as trilhas do controlador.
    bool humanoid_set_ik(u64 layerId, u32 limb, bool on) noexcept;
    /// Controladores IK na tela, kHumanoidIkFloats por membro (4): [0] ligado,
    /// [1..2] alvo na tela, [3..4] polo na tela, [5] key no cabeçote, [6] peso.
    static constexpr u32 kHumanoidIkFloats = 7;
    u32 query_humanoid_ik(u64 layerId, f32* out, u32 capacity) noexcept;
    /// Arrasta o alvo (pole=false) ou o polo do IK no plano da vista.
    bool drag_humanoid_ik(u64 layerId, u32 limb, bool pole, f32 dxPx, f32 dyPx, bool continuing) noexcept;
    /// Marca/tira key do controlador no cabeçote (1 marcou, 0 tirou, −1 erro).
    i32 toggle_humanoid_ik_key(u64 layerId, u32 limb) noexcept;
    // --- Motion Capture (0.3.6 BETA) — rig/Mocap.hpp (EngineMocap.cpp) ---
    /// Uma sessão por motor. As plataformas detectam a pose no aparelho e
    /// mandam os quadros; o motor resolve, mostra a prévia e grava as keys.
    /// Abrir: devolve humanoid_status (2/3 = pronto; 0/1 = sem rig compatível,
    /// a UI oferece o AutoRig 3D) ou −1 (camada inválida). Troca a sessão anterior.
    i32 mocap_begin(u64 layerId) noexcept;
    /// Quadros detectados em lote: rig::kMocapFrameFloats por quadro;
    /// aspect = largura/altura dos quadros analisados.
    bool mocap_push_frames(const f32* data, u32 frames, f32 aspect) noexcept;
    /// Parâmetros (kMocapParams floats): intensidade, suavização, velocidade,
    /// giro °, altura do chão, foot lock, IK das mãos, mover o quadril.
    static constexpr u32 kMocapParams = 8;
    /// Resolve tudo com os parâmetros (bloqueia: chame fora da main; cancelável
    /// com mocap_cancel, progresso em mocap_progress). Atualiza a prévia ligada.
    bool mocap_analyze(const f32* params, u32 count) noexcept;
    void mocap_cancel() noexcept;
    [[nodiscard]] f32 mocap_progress() noexcept;
    /// Relatório (kMocapReportFloats): quadros, duração s, tem 3D, aviso de
    /// baixa confiança, fração incerta por membro ×6 (tronco, cabeça, braço
    /// esq., braço dir., perna esq., perna dir.), pé preso, interpolado.
    static constexpr u32 kMocapReportFloats = 12;
    u32 mocap_report(f32* out, u32 capacity) noexcept;
    /// Prévia: o personagem toca a captura no preview sem gravar keys.
    bool mocap_preview(bool on) noexcept;
    /// Grava as keys (BonePose, com redução) numa ação de desfazer. Devolve
    /// quantas keys entraram (−1 = falhou).
    i32 mocap_apply() noexcept;
    /// Fecha a sessão (tira a prévia).
    void mocap_end() noexcept;
    u32 add_model_cameras(u64 layer) noexcept;
    /// Põe um efeito de PARTE (Animar parte 3D, Esconder partes 3D, Trocar
    /// textura 3D) na camada 3D já apontando para a parte `part` (0 = a 1ª;
    /// no Esconder, primeira = última = ela). Um passo de desfazer. Devolve
    /// o id do efeito.
    [[nodiscard]] Result<u32> add_part_effect(u64 layer, u32 effectType, u32 part) noexcept;
    /// Fonte da camada de texto: família/peso/itálico e, se importada, o arquivo.
    bool set_text_font(u64 layerId, const std::string& family, u32 weight, bool italic, const std::string& path) noexcept;
    /// Estilo de parágrafo (18 floats): modo da caixa, largura, altura, fundo
    /// (liga, rgba, margem, raio), sombra (liga, rgba, dx, dy, desfoque).
    bool set_text_style(u64 layerId, const f32* values, u32 count = 18) noexcept;
    bool query_text_style(u64 layerId, f32* values, u32 capacity = 18) noexcept;
    /// Estilo de um trecho [start, end) em caracteres: cor (opcional), peso
    /// (0 = o do texto), escala. Substitui o que havia no trecho.
    bool set_text_span(u64 layerId, u32 start, u32 end, bool hasColor, Vec4 color, u32 weight, f32 scale) noexcept;
    /// Tira o estilo próprio de [start, end).
    bool clear_text_spans(u64 layerId, u32 start, u32 end) noexcept;

    /// Text Animator. Cada animador = 40 floats (kTextAnimFloats):
    ///  0 ativo, 1 props (TextAnimProp), 2 base (0 letra/1 palavra/2 linha),
    ///  3 tipo (0 intervalo/1 wiggly), 4 forma, 5 ordem aleatória, 6 semente,
    ///  7 início %, 8 fim %, 9 deslocamento %, 10 quantidade %, 11 ease alto,
    ///  12 ease baixo, 13 variações/s, 14-16 posição, 17-18 escala %,
    ///  19-21 rotação°, 22 opacidade %, 23 tracking, 24 desfoque, 25 inclinar,
    ///  26 contorno, 27 deslocar caractere, 28-31 cor, 32-35 cor do contorno,
    ///  36/37 bits animados (seletor: param; props: param-10),
    ///  38/39 bits com keyframe no playhead. Valores = avaliados no playhead.
    static constexpr u32 kTextAnimFloats = 40;
    u32 query_text_animators(u64 layerId, f32* out, u32 capacity) noexcept;
    /// Novo animador com `props`; devolve o índice (−1 = falhou).
    i32 add_text_animator(u64 layerId, u32 props) noexcept;
    bool remove_text_animator(u64 layerId, u32 index) noexcept;
    i32 duplicate_text_animator(u64 layerId, u32 index) noexcept;
    bool move_text_animator(u64 layerId, u32 from, u32 to) noexcept;
    /// Ajustes não animáveis (0..6 e as cores 28..35) de uma vez.
    bool set_text_animator(u64 layerId, u32 index, const f32* v40) noexcept;
    /// Valor de um parâmetro (TextAnimParam): com keyframes, grava no playhead.
    bool set_text_anim_param(u64 layerId, u32 index, u32 param, f32 value) noexcept;
    /// Liga/desliga o keyframe do parâmetro no playhead (losango).
    bool toggle_text_anim_key(u64 layerId, u32 index, u32 param) noexcept;
    /// Preset nativo (substitui os animadores), a partir do início da camada.
    bool apply_text_preset(u64 layerId, u32 preset) noexcept;

    // --- Animadores de CAMADA (qualquer tipo; timeline/LayerAnimator.hpp) -------
    /// Cada animador = 32 floats (kLayerAnimFloats): 0 ativo, 1 unidade (0
    /// camada inteira, 1 letra, 2 palavra, 3 linha), 2 saída, 3 curva (0
    /// linear, 1 suave, 2 entra e sai, 3 passa do ponto), 4 escala Y própria,
    /// 5 semente do wiggle, 6..23 = os LayerAnimParam 0..17 avaliados no
    /// playhead, 24 bits animados (1 << param), 25 bits com keyframe no
    /// playhead, 26 = 1 se a camada é texto ou texto 3D (unidades valem),
    /// 27 loop (vai e volta; também lido pelo set_layer_animator).
    static constexpr u32 kLayerAnimFloats = 32;
    u32 query_layer_animators(u64 layerId, f32* out, u32 capacity) noexcept;
    /// Novo animador (entra desbotando em 1 s a partir do começo da camada);
    /// devolve o índice (−1 = falhou).
    i32 add_layer_animator(u64 layerId) noexcept;
    bool remove_layer_animator(u64 layerId, u32 index) noexcept;
    /// Ajustes não animáveis (0..5). Ligar/desligar a saída espelha os
    /// keyframes do progresso para o fim (ou volta para o começo).
    bool set_layer_animator(u64 layerId, u32 index, const f32* v32) noexcept;
    /// Valor de um parâmetro (LayerAnimParam): com keyframes, grava no playhead.
    bool set_layer_anim_param(u64 layerId, u32 index, u32 param, f32 value) noexcept;
    /// Liga/desliga o keyframe do parâmetro no playhead (losango).
    bool toggle_layer_anim_key(u64 layerId, u32 index, u32 param) noexcept;
    /// Copia os animadores (com keyframes) da camada; devolve quantos.
    u32 copy_layer_animators(u64 layerId) noexcept;
    /// Cola ACRESCENTANDO nas camadas; devolve em quantas colou.
    u32 paste_layer_animators(const u64* ids, u32 count) noexcept;
    /// Quantos animadores estão na área de transferência.
    [[nodiscard]] u32 layer_animator_clipboard() noexcept;

    // --- Animação de TEXTO 3D (presets = animadores de camada por letra) --------
    /// Por modo (entrada, saída, loop) kText3DAnimFloats floats: 0 preset (−1
    /// nenhum), 1 unidade (1 letra, 2 palavra, 3 linha), 2 duração de cada
    /// unidade (s), 3 atraso entre unidades (ms), 4 unidades do texto.
    static constexpr u32 kText3DAnimFloats = 5;
    /// Preenche 3 × kText3DAnimFloats; falso se a camada não é texto 3D.
    bool query_text3d_anim(u64 layerId, f32* out) noexcept;
    /// Um toque aplica: separa as letras (se preciso) e troca o preset do modo
    /// (scene3d::apply_text3d_anim_preset). `preset` < 0 remove. Um desfazer.
    bool apply_text3d_anim(u64 layerId, i32 preset, u32 mode, u32 unit, f32 durationSec, f32 staggerMs) noexcept;

    /// Comprimento do rastro do desfoque de movimento desta camada (× o
    /// obturador da composição; 1 = o do projeto, 0..4).
    bool set_layer_motion_blur_length(u64 layerId, f32 factor) noexcept;
    [[nodiscard]] f32 query_layer_motion_blur_length(u64 layerId) noexcept;

    /// Camadas que um ajuste afeta: 0 = todas abaixo, 1 = só a logo abaixo,
    /// 2 = só as camadas escolhidas (a lista do app antigo, abaixo do ajuste).
    bool set_adjustment_scope(u64 layerId, u32 scope) noexcept;
    [[nodiscard]] u32 query_adjustment_scope(u64 layerId) noexcept;
    /// Escopo 2: põe/tira `targetId` da lista do ajuste (passa o escopo a 2).
    bool set_adjustment_target(u64 layerId, u64 targetId, bool on) noexcept;
    /// A lista do escopo 2 (ids das camadas); devolve quantas.
    u32 query_adjustment_targets(u64 layerId, u64* out, u32 capacity) noexcept;

    // --- Presets de efeito (Effect::presets) --------------------------------------
    /// Os presets do tipo: (id estável, nome do motor). Vazio = sem presets.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> effect_presets(u32 typeId) noexcept;
    /// Aplica o preset `preset` ao efeito `effectId` da camada: escreve os
    /// valores parados dos parâmetros dele num passo de desfazer.
    bool apply_effect_preset(u64 layerId, u32 effectId, u32 preset) noexcept;
    // --- Roto Brush do Rotobrush IA (EngineRoto.cpp) ---------------------------
    /// Um traço no quadro atual: `xy` em px da COMPOSIÇÃO (x,y intercalados),
    /// raio em px da composição. O motor leva à camada e guarda no efeito
    /// (um passo de desfazer). `background` = traço de fundo (vermelho).
    bool roto_add_stroke(u64 layerId, u32 effectId, bool background, f32 radius, const f32* xy, u32 count) noexcept;
    /// Desfaz o último traço do quadro atual (sem traço aqui: o último de todos).
    bool roto_undo_stroke(u64 layerId, u32 effectId) noexcept;
    /// out3: [0] traços no quadro atual, [1] traços no total, [2] quadros-base.
    bool roto_stroke_info(u64 layerId, u32 effectId, u32* out3) noexcept;
    /// "Propagar clipe": recortes de todos os quadros do clipe no worker.
    bool roto_propagate(u64 layerId, u32 effectId) noexcept;
    /// out4: feitos, total, rodando (0/1), falhou (0/1). Falso = nada propagado.
    bool roto_progress(i64* out4) noexcept;
    /// Estado do Roto desta camada+efeito (Renderer::roto_status): out8 =
    /// [propagados, total, propagando, falhou, quadro atual pronto, quadros do
    /// clipe prontos, quadros do clipe, há traços]. Falso = sem traços.
    bool roto_status(u64 layerId, u32 effectId, i64* out8) noexcept;
    void roto_cancel() noexcept;
    /// Modo de vista (0 final, 1 máscara, 2 sobreposição) sem passo de desfazer:
    /// o modo de pintura liga a sobreposição e devolve o anterior ao sair.
    bool roto_set_view(u64 layerId, u32 effectId, u32 mode) noexcept;
    /// Grupo (pré-composição): a câmera de fora alcança as camadas 3D de dentro.
    bool set_group_camera_pass_through(u64 layerId, bool on) noexcept;
    /// −1 = não é grupo; 0/1.
    [[nodiscard]] i32 query_group_camera_pass_through(u64 layerId) noexcept;
    /// "Aceita luzes": a camada 2D no espaço 3D recebe as luzes da composição
    /// (Layer::acceptsLights; desligado por padrão, como os projetos antigos).
    bool set_layer_accepts_lights(u64 layerId, bool on) noexcept;
    /// Persistently opt a flat layer into the scene camera without altering
    /// its transform or keyframes. Undoable; retained when saving the project.
    bool enable_layer_3d(u64 layerId) noexcept;
    /// Toggle a flat layer's 3D participation without changing its animation.
    bool set_layer_3d(u64 layerId, bool on) noexcept;
    /// Opt-in editing policy: all navigation stops at the last layer's end.
    void set_content_bounded_playback(bool on) noexcept;
    [[nodiscard]] i64 query_navigation_end() noexcept;
    /// −1 = camada sem essa opção (câmera, luz, modelo 3D, áudio, nulo); 0/1.
    [[nodiscard]] i32 query_layer_accepts_lights(u64 layerId) noexcept;
    /// Põe as camadas dentro do grupo `groupLayerId` (mesma composição), com
    /// os mesmos tempos na tela. Recusa (motivo em `why`) grupo com o tempo
    /// alterado. Desfazível num passo. Devolve quantas entraram.
    [[nodiscard]] Result<u32> add_layers_to_group(const u64* ids, u32 count, u64 groupLayerId, std::string* why = nullptr) noexcept;
    /// Dentro de um grupo aberto: tira a camada dele — ela volta para a
    /// composição de fora, logo acima do grupo, com os mesmos tempos (e presa
    /// ao grupo como pai se ele tem transform). A timeline volta para fora.
    /// Devolve a camada nova.
    [[nodiscard]] Result<u64> remove_layer_from_group(u64 layerId, std::string* why = nullptr) noexcept;

    // =========================================================================
    // Expressões (expr/Expression.hpp). A trilha é (property, effectIndex,
    // paramIndex) — a mesma chave dos keyframes: transform com effectIndex =
    // kInvalidIndex e paramIndex 0; efeito com o id do efeito e
    // param_track_key(parâmetro, componente); animador de texto com o índice
    // do animador e o TextAnimParam; TrackProperty::TimeRemap = o remap.
    // =========================================================================
    /// Liga (ou troca) a expressão da trilha; fonte vazia remove. Com erro de
    /// sintaxe a expressão fica gravada (a propriedade usa os keyframes até a
    /// pessoa corrigir) e `diag` diz onde. Desfazível.
    Status set_expression(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex, const char* source,
                          expr::Diagnostic* diag = nullptr) noexcept;
    /// Liga/desliga sem apagar o texto. Desfazível.
    bool set_expression_enabled(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex, bool enabled) noexcept;
    /// Várias trilhas com o MESMO texto num passo de desfazer (Posição = X e Y;
    /// a expressão é vetorial e cada trilha pega o seu componente). `keys3` =
    /// `count` trincas (property, effectIndex, paramIndex), no máximo 16.
    Status set_expressions(u64 layerId, const u32* keys3, u32 count, const char* source,
                           expr::Diagnostic* diag = nullptr) noexcept;
    bool set_expressions_enabled(u64 layerId, const u32* keys3, u32 count, bool enabled) noexcept;
    struct ExpressionInfo {
        bool             exists = false;
        bool             enabled = true;
        std::string      source;
        expr::Diagnostic error;       ///< sintaxe ou execução (avaliada no playhead agora)
        f32              value = 0;   ///< valor resultante no playhead (unidade guardada)
    };
    bool query_expression(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex, ExpressionInfo& out) noexcept;
    /// Trilhas com expressão na camada: 4 u32 por linha (property, effectIndex,
    /// paramIndex, flags: 1 ligada, 2 com erro). Devolve o número de linhas.
    u32 query_expressions(u64 layerId, u32* out, u32 capacityRows) noexcept;

    /// Arquivo de mídia da camada de vídeo/áudio (caminho ou content://), para
    /// o provedor de transcrição ler o áudio. Vazio = não tem mídia.
    [[nodiscard]] std::string layer_media_path(u64 layerId) noexcept;
    [[nodiscard]] Result<std::vector<text::CaptionWord>> transcribe_local(u64 layerId, const std::string& model, const std::string& language, bool translateEnglish = false) noexcept;
    std::atomic<int> captionProgress{0};
    std::atomic<bool> captionCancelled{false};
    [[nodiscard]] std::string caption_tracks() noexcept;
    [[nodiscard]] bool edit_caption_track(u64 layerId, const std::string& command) noexcept;
    [[nodiscard]] std::string save_caption_bundle(u64 layerId, const std::string& name) noexcept;
    [[nodiscard]] bool apply_caption_bundle(u64 layerId, const std::string& data) noexcept;
    /// Blocos de TODAS as faixas de legendas numa travessia, na ordem das
    /// camadas e por tempo dentro de cada faixa — é o que a timeline desenha.
    /// Devolve `count << 32 | total`: `count` blocos escritos (até `capacity`) e
    /// `total` que existem. Se qualquer um passar do buffer, o chamador cresce e
    /// pergunta de novo (mesma regra de `query_all_keyframes`).
    [[nodiscard]] u64 query_captions(bridge::CaptionRow* rows, u32 capacity, char* text, u32 textCapacity) noexcept;
    /// Legendas da fala da camada `sourceLayer` (palavras em segundos da
    /// mídia): camadas de texto no tempo da fala, num passo de desfazer.
    /// Substitui as legendas anteriores dessa camada. Devolve quantas criou.
    [[nodiscard]] Result<u32> create_captions(u64 sourceLayer, const std::vector<text::CaptionWord>& words,
                                              const text::CaptionOptions& options) noexcept;
    /// Tira as legendas geradas da camada. Devolve quantas tirou.
    u32 remove_captions(u64 sourceLayer) noexcept;
    [[nodiscard]] u32 caption_count(u64 sourceLayer) noexcept;
    /// "família\tpeso\titálico\tcaminho real" da camada de texto (vazio = não é texto).
    [[nodiscard]] std::string text_font(u64 layerId) noexcept;

    /// Nova camada de texto ("Texto", centralizada), do cabeçote até o fim.
    [[nodiscard]] Result<u64> add_text(const char* content = nullptr) noexcept;
    /// Nulo (não desenha; serve de pai/controle). `threeD` = nulo 3D (vive na
    /// cena: posição/rotação/escala em X, Y e Z).
    [[nodiscard]] Result<u64> add_null(bool threeD) noexcept;
    /// "Vincular a novo nulo": cria um nulo no centro (mundo) das camadas e
    /// faz dele o pai de todas, com a compensação de LayerSetParent — nada
    /// sai do lugar na tela e as animações próprias ficam. Nulo 3D se alguma
    /// camada é 3D. Tudo num passo de desfazer. Devolve o id do nulo.
    [[nodiscard]] Result<u64> parent_to_new_null(const u64* layerIds, u32 count) noexcept;
    /// "Escalonar": em cascata, na ordem de `layerIds`, a i-ésima camada
    /// desbloqueada anda i × `stepFrames` quadros (a primeira fica). Com
    /// `keysOnly`, só a ANIMAÇÃO anda — keyframes de todas as trilhas, das
    /// máscaras e das formas vetoriais; a barra e o conteúdo ficam. Sem ele, a
    /// camada inteira anda (conteúdo e keyframes junto). Um passo de desfazer.
    /// Devolve quantas camadas andaram.
    [[nodiscard]] Result<u32> stagger_layers(const u64* layerIds, u32 count, i64 stepFrames, bool keysOnly) noexcept;
    /// Atomic timing arrangement; skips locked/missing/duplicate IDs. Sequence
    /// follows the supplied display order; distributions sort by start time.
    /// Keeps duration, source offset, local animation, and unselected layers.
    /// No ripple; invalid bounds/gaps reject the entire plan, without history.
    [[nodiscard]] Result<u32> arrange_layer_times(const u64* layerIds, u32 count,
        LayerTimeArrangement mode, i64 playhead = 0) noexcept;
    [[nodiscard]] Result<u64> add_camera() noexcept;
    // Both native interfaces send a display-oriented OpenGL pose. Capture
    // state, timeline time, key creation and undo belong to the shared engine.
    struct RealCameraStatus {
        bool active = false;
        bool recording = false;
        i64 lastFrame = -1;
        bool positional = false;
    };
    [[nodiscard]] Status real_camera_start(u64 packedLayer) noexcept;
    [[nodiscard]] Status real_camera_record(bool recording) noexcept;
    [[nodiscard]] Status real_camera_sample(const f32* pose16, u64 timestampNs,
                                          bool positional, f32 sensitivity, bool inverted) noexcept;
    [[nodiscard]] Status real_camera_recenter() noexcept;
    /// Andar (m/s, + frente) e profundidade (× do deslocamento frente/trás do AR).
    [[nodiscard]] Status real_camera_walk(f32 metersPerSecond, f32 depthScale) noexcept;
    /// Tracking is lost: record the last valid virtual pose as a constant.
    [[nodiscard]] Status real_camera_hold() noexcept;
    void real_camera_stop() noexcept;
    [[nodiscard]] RealCameraStatus real_camera_status() noexcept;
    /// 0 directional, 1 point, 2 spot, 3 point light with a camera-projected flare.
    [[nodiscard]] Result<u64> add_light(u32 kind) noexcept;
    [[nodiscard]] u32 query_materials(u64 layer, f32* values, u32 capacity) noexcept;
    [[nodiscard]] Status set_material_param(u64 layer, u32 material, u32 param, f32 value) noexcept;
    [[nodiscard]] bool query_light(u64 layer, f32* values, u32 capacity = 10) noexcept;
    /// Lente da câmera 3D no cabeçote (9 valores): [0] distância focal (mm,
    /// a que a projeção usa), [1] FOV vertical (°) calculado dela (sensor full
    /// frame 36×24, altura 24 mm), [2] DOF ligado, [3] distância de foco (px do
    /// mundo), [4] abertura (f/), [5] força do desfoque (×), [6] px por metro
    /// da cena, [7] máscara de trilhas com keyframe (bit 0 mm, 1 foco,
    /// 2 abertura, 3 desfoque), [8] câmera ativa. Capacity >= 10 also returns
    /// [9] whether the camera projects visual layers below it (Timeline v47).
    /// The default capacity preserves existing nine-value callers.
    [[nodiscard]] bool query_camera_lens(u64 layer, f32* values, u32 capacity = 9) noexcept;
    /// Pick Focus: distância AO LONGO DO EIXO ÓTICO da câmera `layer` até a
    /// superfície 3D sob o ponto (px da composição), com o raio da câmera
    /// ativa (a que o palco mostra). Só mede; a UI grava com
    /// LayerSetCameraParam (um passo de desfazer). < 0 = nada 3D ali.
    [[nodiscard]] f32 pick_focus_distance(u64 layer, f32 compX, f32 compY) noexcept;
    // Diagnostic source playback: transient, never changes the project or export.
    bool set_raw_playback(bool enabled) noexcept;
    std::string playback_report() noexcept;
    void set_scene_editor(bool enabled, f32 yaw, f32 pitch, f32 distance) noexcept;
    [[nodiscard]] u32 query_scene_guides(f32* lines, u32 capacity) noexcept;
    /// Cena 3D: a camada 3D visível sob o ponto (px da composição), pelo raio da
    /// câmera de navegação contra o corpo real (plano da caixa ou triângulos do
    /// modelo), a mais perto ganha; sem corpo ali, a origem mais perto na tela
    /// até `radius` px. 0 = nada.
    [[nodiscard]] u64 scene_pick(f32 compX, f32 compY, f32 radius) noexcept;

    /// Remapeamento de tempo: ligar cria a curva equivalente ao tempo atual
    /// (nada muda até editar); desligar volta à velocidade (a curva fica guardada).
    bool set_time_remap(u64 layerId, bool on) noexcept;
    bool set_time_remap_value(u64 layerId, i64 localFrame, f32 sourceFrame) noexcept;
    /// Rampa de velocidade pronta sobre o trecho da fonte atual: 0 linear,
    /// 1 suave (entrada e saída), 2 herói (rápido-lento-rápido), 3 acelerar,
    /// 4 desacelerar, 5 congelar o clipe inteiro no cabeçote, 6 de trás para
    /// frente, 7 congelar aqui (1 s parado no cabeçote e segue normal; as
    /// chaves antes do cabeçote ficam). Liga o remapeamento.
    bool apply_speed_ramp(u64 layerId, u32 preset) noexcept;
    /// Curva de tempo para o editor de gráfico. `out` = {nº de pontos, início
    /// e fim locais da camada, último quadro da fonte (quadros da composição),
    /// velocidade no cabeçote} e depois 7 floats por ponto: tempo local, quadro
    /// da fonte, interpolação, bx1, by1, bx2, by2. Devolve os floats escritos.
    u32 query_time_remap(u64 layerId, f32* out, u32 maxFloats) noexcept;
    /// Momento atual da fonte, em quadros da composição, avaliado pelo motor
    /// (inclui remap/easing/expressão). NaN quando projeto/camada não existem.
    [[nodiscard]] f64 query_time_remap_current_source(u64 layerId) noexcept;
    /// Ponto da curva: `index` < 0 insere em `localFrame` com o valor que a
    /// curva JÁ tem ali (nada muda até mover); senão move o ponto (tempo
    /// preso entre os vizinhos, valor dentro da fonte). `interp` < 0 mantém.
    /// Devolve o índice do ponto, ou −1.
    i32 edit_time_remap_key(u64 layerId, i32 index, i64 localFrame, f32 sourceFrame, i32 interp) noexcept;
    /// Apaga um ponto (ficam pelo menos dois).
    bool remove_time_remap_key(u64 layerId, u32 index) noexcept;
    /// "Ao contrário" do Remapear tempo: espelha a curva no tempo do clipe
    /// (o primeiro quadro mostra o que o último mostrava e vice-versa), com o
    /// easing de cada trecho espelhado — duas vezes volta exatamente ao que era.
    /// Com o remapeamento desligado, liga antes (curva = o tempo de agora).
    bool reverse_time_remap(u64 layerId) noexcept;
    /// Gráfico de velocidade (painel de velocidade, 0.1.8). `out` = {nº de
    /// amostras S, nº de chaves K, início e fim locais da camada, cabeçote
    /// local, modo (0 constante, 1 curva de velocidade, 2 remapeamento),
    /// velocidade no cabeçote (×), quadro da fonte no cabeçote (quadros da
    /// composição)}; depois S velocidades (×) igualmente espaçadas do início
    /// ao fim (no remapeamento, a inclinação da curva de tempo) e K×3 (tempo
    /// local, velocidade, interpolação) das chaves de VELOCIDADE.
    u32 query_speed_curve(u64 layerId, u32 samples, f32* out, u32 maxFloats) noexcept;
    /// Curva de velocidade pronta (desliga o remapeamento): chaves de
    /// VELOCIDADE suaves com a MESMA quantidade de fonte que o clipe mostra
    /// agora (a barra não muda). 0 plana, 1 montagem, 2 herói, 3 bala,
    /// 4 entrada rápida, 5 saída rápida, 6 pulo, 7 suavizar as chaves que
    /// existem, 8 tirar a curva (volta à velocidade constante média).
    bool apply_speed_curve(u64 layerId, u32 preset) noexcept;
    /// Chave de velocidade pelo gráfico: `index` < 0 insere em `localFrame`
    /// com a velocidade que a curva já tem ali (nada muda até mover); senão
    /// move (tempo preso entre as vizinhas) e muda a velocidade (×, 0,05…16).
    /// `interp` < 0 mantém. Devolve o índice da chave, ou −1.
    i32 edit_speed_key(u64 layerId, i32 index, i64 localFrame, f32 speed, i32 interp) noexcept;
    /// Apaga uma chave de velocidade (a última que sobra vira velocidade constante).
    bool remove_speed_key(u64 layerId, u32 index) noexcept;
    /// Manter o tom do áudio quando a fonte não anda a 1× (Layer::keepPitch).
    bool set_keep_pitch(u64 layerId, bool on) noexcept;

    /// Transição de entrada (`out` = false) ou saída: tipo (0 nenhuma, 1
    /// dissolver, 2 deslizar p/ cima, 3 deslizar da esquerda, 4 zoom, 5 girar)
    /// e duração em quadros (limitada a metade da camada).
    bool set_transition(u64 layerId, bool out, u32 type, u32 frames) noexcept;

    /// Rastreia o ponto (px da camada de vídeo, no quadro do CABEÇOTE) dali
    /// até o fim do clipe. `stabilize` = false cria um Nulo "Rastreio" que segue o
    /// ponto; true move a própria camada para o ponto ficar parado na tela.
    /// Síncrono (decodifica o vídeo): fora da thread de UI. Devolve o id da
    /// camada que recebeu os keyframes; `tracked` = quadros rastreados.
    [[nodiscard]] Result<u64> track_point(u64 layerId, f32 x, f32 y, bool stabilize, u32* tracked = nullptr) noexcept;

    // --- Máscaras (roto) e track matte ----------------------------------------------
    /// Pontos de caminho = 6 floats cada: x, y, tangente de entrada x/y, de
    /// saída x/y (px da camada, tangentes relativas ao ponto).
    /// Máscara nova com o caminho dado. Devolve o id (−1 = falhou).
    i32 add_mask(u64 layerId, const f32* pts6, u32 count, bool closed) noexcept;
    bool remove_mask(u64 layerId, u32 maskId) noexcept;
    /// Troca o caminho inteiro. Com o caminho animado, grava/atualiza o key no
    /// CABEÇOTE. `undo` = abre um passo de desfazer (false nos eventos
    /// seguintes de um arrasto, que entram no mesmo passo).
    bool set_mask_path(u64 layerId, u32 maskId, const f32* pts6, u32 count, bool closed, bool undo) noexcept;
    /// Modo (MaskOperation: 0 somar, 1 subtrair, 2 intersectar, 3 diferença,
    /// 4 nenhum), invertida, feather e expansão (px da camada), opacidade 0..1.
    bool set_mask_props(u64 layerId, u32 maskId, u32 op, bool inverted, f32 feather, f32 expansion, f32 opacity) noexcept;
    bool set_mask_param(u64 layerId, u32 maskId, u32 param, f32 value) noexcept;
    bool toggle_mask_param_key(u64 layerId, u32 maskId, u32 param) noexcept;
    /// Liga/desliga o key do caminho no cabeçote (`keyed` = ficou com key).
    bool toggle_mask_path_key(u64 layerId, u32 maskId, bool* keyed = nullptr) noexcept;
    /// Marca o keyframe do caminho da máscara aqui — com keyframe no cabeçote,
    /// REGRAVA a forma avaliada em vez de apagá-la (ver `ensure_shape_param_key`).
    bool ensure_mask_path_key(u64 layerId, u32 maskId, bool* keyed = nullptr) noexcept;
    /// Máscaras no cabeçote: [0..5] composição ← camada (a b c d tx ty), [6] nº
    /// de máscaras; por máscara kMaskHeaderFloats floats (id, modo, invertida,
    /// feather, expansão, opacidade, fechada, nº de pontos, nº de keys, key no
    /// cabeçote, ativa, 0) e 6 por ponto (a forma no cabeçote). Devolve os
    /// floats necessários; só escreve se `capacity` couber.
    static constexpr u32 kMaskHeaderFloats = 12;
    u32 query_masks(u64 layerId, f32* out, u32 capacity) noexcept;

    // --- Rig 2D (personagem desenhado; camada de imagem) — timeline/Rig.hpp ----
    /// Juntas no playhead: por junta kRigJointFloats floats (id, id do pai ou
    /// −1, x, y na COMPOSIÇÃO, 1 se o osso tem keyframe no playhead). `bind` =
    /// posições de montagem (senão, a pose). Devolve o nº de floats precisos
    /// (0 = não é imagem); com `capacity` menor não escreve nada.
    static constexpr u32 kRigJointFloats = 5;
    u32 query_rig(u64 layerId, bool bind, f32* out, u32 capacity) noexcept;
    /// Nova junta em (x, y) da composição ligada a `parentId` (−1 = raiz
    /// solta). Devolve o id da junta (−1 = falhou). Um passo de desfazer.
    i32 rig_add_joint(u64 layerId, i32 parentId, f32 compX, f32 compY) noexcept;
    /// Move a junta na MONTAGEM. `continuing` = o mesmo arrasto (um desfazer).
    bool rig_move_joint(u64 layerId, u32 jointId, f32 compX, f32 compY, bool continuing) noexcept;
    /// Apaga a junta (e os keyframes do osso dela); os filhos passam ao pai dela.
    bool rig_remove_joint(u64 layerId, u32 jointId) noexcept;
    /// Tira o rig inteiro da camada (juntas e keyframes dos ossos).
    bool rig_clear(u64 layerId) noexcept;
    /// Esqueleto automático de personagem sobre a parte visível da imagem
    /// (quadril, coluna, pescoço, cabeça, braços; pernas quando o desenho é
    /// de corpo inteiro). Troca o rig que houver. Devolve quantas juntas.
    u32 rig_auto_humanoid(u64 layerId) noexcept;
    /// ANIMAR: leva a junta até (x, y) da composição e grava keyframe no
    /// playhead. Ponta de cadeia com avô = IK de 2 ossos (a mão puxa o braço);
    /// junta com filhos ou só um osso acima = gira o osso dela (FK).
    bool rig_pose_joint(u64 layerId, u32 jointId, f32 compX, f32 compY, bool continuing) noexcept;
    /// Montagem aberta nesta camada: o preview a mostra sem deformação (0 = nenhuma).
    void set_rig_setup_layer(u64 layerId) noexcept;
    /// MALHA DE DEFORMAÇÃO no palco (EngineMeshWarp.cpp). Cabeçalho de
    /// `kMeshWarpHeaderFloats` {linhas, colunas, key no cabeçote, animada} e
    /// 10 floats por vértice NORMALIZADOS à caixa da camada (0..1): posição e
    /// as alças esquerda, direita, cima, baixo RELATIVAS — a UI leva à tela
    /// pelos cantos da camada. Devolve o nº de floats (0 = não é o efeito).
    static constexpr u32 kMeshWarpHeaderFloats = 4;
    u32 query_mesh_warp(u64 layerId, u32 effectId, f32* out, u32 capacity) noexcept;
    /// Leva o vértice (`handle` 0; as alças vão junto) ou uma alça (1 esq, 2 dir,
    /// 3 cima, 4 baixo) até (u, v) normalizado. Malha animada ou `autoKey` =
    /// key no cabeçote; senão a malha parada. `continuing` = o mesmo arrasto
    /// (um passo de desfazer por gesto).
    bool mesh_warp_drag(u64 layerId, u32 effectId, u32 vertex, u32 handle, f32 u, f32 v, bool autoKey,
                        bool continuing) noexcept;
    /// "Redefinir malha": a de fábrica, sem keys. Um passo de desfazer.
    bool mesh_warp_reset(u64 layerId, u32 effectId) noexcept;
    bool mesh_warp_toggle_key(u64 layerId, u32 effectId) noexcept;
    // --- Fantoche (aurea.distort.puppet; effects/Puppet.hpp) — EnginePuppet.cpp.
    // u, v = fração da caixa da camada (como a malha de deformação).
    /// Por pino ligado kPuppetPinFloats floats: índice, u, v (posição no
    /// playhead), 1 = keyframe neste instante. Devolve o necessário.
    static constexpr u32 kPuppetPinFloats = 4;
    u32 query_puppet(u64 layerId, u32 effectId, f32* out, u32 capacity) noexcept;
    /// Pino novo sob o toque (−1 = cheio/sem efeito). Um passo de desfazer.
    i32 puppet_add_pin(u64 layerId, u32 effectId, f32 u, f32 v) noexcept;
    /// Arrasto do pino; `continuing` = mesmo gesto (sem passo novo).
    bool puppet_move_pin(u64 layerId, u32 effectId, u32 pin, f32 u, f32 v, bool autoKey, bool continuing) noexcept;
    bool puppet_remove_pin(u64 layerId, u32 effectId, u32 pin) noexcept;
    /// Malha deformada no playhead (o mesmo contorno e densidade do render):
    /// 6 floats por triângulo (u, v dos 3 vértices). Devolve o necessário.
    u32 query_puppet_mesh(u64 layerId, u32 effectId, f32* out, u32 capacity) noexcept;
    /// Tamanho natural da camada (px) e contorno opaco dela agora — imagem
    /// (alfa) ou recorte do Rotobrush já calculado. Sob modelMutex_.
    void puppet_layer_size(const Layer& l, f32& w, f32& h) const noexcept;
    bool puppet_capture_outline(const Composition& comp, const Layer& l, FrameIndex local, f32* rows) noexcept;
    /// Rastreia a máscara no vídeo da camada do cabeçote em diante (NCC no
    /// centro e em 4 pontos por dentro dela) e grava um key de caminho por
    /// quadro. `mode` 0 = só posição; 1 = posição + escala + giro. Síncrono
    /// (decodifica): fora da thread de UI. Devolve os quadros rastreados.
    [[nodiscard]] Result<u32> track_mask(u64 layerId, u32 maskId, u32 mode) noexcept;
    /// Track matte: a camada aparece através da `matteLayerId` (MatteMode: 1
    /// alfa, 2 alfa invertido, 3 luma, 4 luma invertido; 0 ou matte 0 = tira).
    bool set_track_matte(u64 layerId, u64 matteLayerId, u32 mode) noexcept;
    bool query_track_matte(u64 layerId, u64& matte, u32& mode) noexcept;

    /// Eco (0 = desligado; atraso em quadros; queda 0..1) e RGB no tempo
    /// (atraso em quadros, 0 = desligado).
    bool set_echo(u64 layerId, u32 count, f32 delay, f32 decay) noexcept;
    bool set_rgb_time(u64 layerId, f32 delay) noexcept;
    /// {cópias, atraso, queda, atraso RGB}.
    bool query_echo(u64 layerId, f32* out4) noexcept;

    /// Partículas (GPU, analíticas). Presets: 0 faíscas, 1 neve, 2 poeira de luz.
    [[nodiscard]] Result<u64> add_particles(u32 preset) noexcept;
    /// 0.4.1 (mockup "interface 03", Elementos): camada de ajuste 3D "Aurora" com o efeito
    /// Aurora (Gerar) já aplicado, cobrindo o quadro todo. Um passo de desfazer.
    [[nodiscard]] Result<u64> add_aurora() noexcept;
    bool apply_particle_preset(u64 layerId, u32 preset) noexcept;
    /// 0 taxa, 1 vida, 2 velocidade, 3 espalhamento, 4 gravidade, 5 tamanho
    /// inicial, 6 tamanho final, 7 direção.
    bool set_particle_param(u64 layerId, u32 param, f32 value) noexcept;
    /// Os 8 parâmetros acima em `out`.
    /// Um valor por ParticleParam, na ordem do enum (`ParticleParam::Count` no total).
    bool query_particles(u64 layerId, f32* out) noexcept;
    /// 8.2 — o que vem de OUTRA camada/asset (cada troca = um passo de desfazer).
    /// Camada que emite (emissores Camada/Texto/Caminho/Malha); 0 = nenhuma.
    bool set_particle_source(u64 layerId, u64 sourceLayerId) noexcept;
    /// Imagem da partícula de textura: uma CAMADA de imagem (usa a imagem
    /// dela) ou um AssetId de imagem carregado; 0 = nenhuma.
    bool set_particle_texture(u64 layerId, u64 assetOrImageLayer) noexcept;
    /// Modelo da partícula de malha: camada de modelo 3D; 0 = nenhuma.
    bool set_particle_mesh(u64 layerId, u64 modelLayerId) noexcept;
    /// Curvas ao longo da vida, até 8 pontos (`count`; 0 = volta às pontas
    /// início/fim). kind 0 = cor (posição, r, g, b sRGB — 4 floats por ponto),
    /// 1 = tamanho (posição, multiplicador), 2 = opacidade (posição, 0..1).
    bool set_particle_life_curves(u64 layerId, u32 kind, const f32* values, u32 count) noexcept;
    /// {fonte, 1ª camada de imagem com a textura (ou 0), malha, asset da textura}.
    bool query_particle_links(u64 layerId, u64* out4) noexcept;
    /// Pontos da curva `kind` em `out` (4 ou 2 floats por ponto); devolve quantos.
    u32 query_particle_curve(u64 layerId, u32 kind, f32* out, u32 capacity) noexcept;

    /// Desfoque de movimento da camada (liga também o da composição).
    bool set_motion_blur(u64 layerId, bool on) noexcept;
    /// Camada de ajuste: os efeitos dela valem para tudo o que está abaixo.
    bool set_layer_adjustment(u64 layerId, bool on) noexcept;
    /// Guia: aparece no preview e nunca sai no export.
    bool set_layer_guide(u64 layerId, bool on) noexcept;
    /// Etiqueta de cor (0 = nenhuma, até kLayerLabelCount − 1).
    bool set_layer_label(u64 layerId, u32 label) noexcept;
    /// Máscara de recorte ("Criar/Liberar máscara de recorte"): a camada só
    /// aparece onde a camada de baixo (ordem de desenho) tem alfa. Criar exige
    /// camada visual com base visual abaixo (`can_create_clip`); liberar solta
    /// também a cadeia recortada logo acima. Um passo de desfazer.
    bool set_layer_clip_to_below(u64 layerId, bool on) noexcept;
    /// Solo: com alguma camada em solo, o preview (e o áudio) só tocam as em solo.
    bool set_layer_solo(u64 layerId, bool on) noexcept;
    /// Camadas da composição atual cujo nome ou texto contém `query` (sem
    /// diferença de maiúsculas nem de acento: "titulo" acha "TÍTULO"). Da
    /// frente para o fundo, como a timeline mostra. Consulta vazia = nenhuma.
    [[nodiscard]] std::vector<u64> search_layers(const std::string& query) noexcept;
    /// Vídeo em câmera lenta/velocidade quebrada: 0 repete o quadro, 1 mistura
    /// os dois quadros vizinhos da fonte, 2 movimento de pixels (optical flow).
    bool set_frame_blend(u64 layerId, u32 mode) noexcept;
    /// Desfoque pelo movimento do vídeo (optical flow): 0 desliga; 1 = o
    /// obturador da composição (até 2).
    bool set_vector_blur(u64 layerId, f32 amount) noexcept;
    /// Acertos/erros do cache do optical flow do renderer.
    void flow_cache_stats(u32& hits, u32& misses) const noexcept { renderer_.flow_cache_stats(hits, misses); }
    void set_flow_cache_enabled(bool on) noexcept { renderer_.set_flow_cache_enabled(on); }
    /// Coberturas de máscara reaproveitadas/rasterizadas pelo renderer.
    void mask_cache_stats(u32& hits, u32& misses) const noexcept { renderer_.mask_cache_stats(hits, misses); }
    /// Obturador da composição em graus (0–720; 180 = padrão de cinema).
    bool set_shutter_angle(f32 degrees) noexcept;
    /// Chave geral da composição (as camadas com desfoque só borram com ela).
    bool set_composition_motion_blur(bool on) noexcept;
    /// {ligado, obturador em graus} da composição atual.
    bool query_motion_blur(bool& on, f32& shutter) noexcept;
    bool query_motion_blur_settings(MotionBlurSettings& out) noexcept;
    /// One undoable composition edit. Rejects nonfinite values and invalid sample counts.
    bool set_motion_blur_settings(bool enabled, f32 angle, f32 phase,
                                  u32 samples, u32 adaptiveLimit) noexcept;
    /// 0.3.2: motion blur 3D da composição — true = rápido (vetores de
    /// movimento, uma cena por quadro), false = alta qualidade (amostras).
    /// Grupo que os vetores não representam fica nas amostras. Desfazível.
    bool set_motion_blur_vector(bool on) noexcept;

    // --- Rastreio de câmera 3D -----------------------------------------------------
    /// Estado da análise: 0 parado, 1 analisando, 2 pronto, 3 falhou, 4 cancelado.
    struct CameraTrackStatus {
        u32 state = 0;
        f32 progress = 0.0f;
        u32 frames = 0, framesSolved = 0, tracks = 0, inliers = 0;
        f32 rmsError = 0.0f, confidence = 0.0f, fovDeg = 0.0f;
        bool rotationOnly = false;
        bool cached = false;              ///< resultado veio do cache (vídeo e ajustes iguais)
        std::string message;
        // Tipo de tomada, método e lente que o solve USOU (tracking::CameraSolution).
        u32 shotType = 0;                 ///< 0 FOV fixa, 1 zoom variável, 2 FOV informada
        u32 method = 1;                   ///< 1 típico, 2 tripé, 3 cena quase plana
        f32 fovStartDeg = 0.0f, fovEndDeg = 0.0f;   ///< zoom variável: FOV do 1º e do último quadro
        f32 fovMinDeg = 0.0f, fovMaxDeg = 0.0f;     ///< e a faixa no trecho
        f32 k1 = 0.0f, k2 = 0.0f;         ///< distorção radial (meia altura = 1)
        bool lensSolved = false, k2Solved = false;
        f32 averageErrorPx = 0.0f;        ///< "Average Error" do AE: px da FONTE
    };
    /// Analisa o vídeo da camada em segundo plano (0 rápido, 1 equilibrado,
    /// 2 alta qualidade). Uma análise por vez; a UI continua navegando.
    /// Camada acima de tracking::kMaxCameraTrackFrames: devolve true com o
    /// estado 3 e a mensagem do porquê (dividir o clipe).
    /// `motion` = método (0 automático, 1 típico, 2 tripé, 3 cena quase plana);
    /// `shotType` = 0 FOV fixa, 1 zoom variável, 2 FOV informada (exige
    /// `knownFov` > 0); `lens` = 1 estima a distorção radial (k1; k2 se estável).
    bool start_camera_track(u64 layerId, u32 mode, u32 motion = 0, f32 knownFov = 0, u32 shotType = 0,
                            u32 lens = 0) noexcept;
    /// Remove selected feature tracks and re-solve the saved observations.
    /// No decode, no UI blocking; previous camera/objects stay until applied.
    bool refine_camera_track(bool removeSelected = true, u32 motion = 0, f32 knownFov = 0, u32 shotType = 0,
                             u32 lens = 0) noexcept;
    /// Por quadro analisado: {FOV vertical em graus, erro médio em px da fonte
    /// (−1 = sem pose)}. Devolve quantos quadros escreveu (até `capacity`).
    u32 camera_track_frame_info(f32* out2, u32 capacity) noexcept;
    /// Põe o efeito "Distorção da lente" (aurea.distort.lens_distortion_match)
    /// com o k1/k2 resolvidos na camada `layerId`: `redistort` = false remove a
    /// distorção (no vídeo), true aplica (na CG). Um passo de desfazer; devolve
    /// o id do efeito. Falha sem solve com lente.
    [[nodiscard]] Result<u32> add_camera_track_lens_effect(u64 layerId, bool redistort) noexcept;
    /// 0 selected origin, 1 selected ground/origin, 2 distance between two
    /// selected points in scene units. Rebase camera and anchors together.
    bool calibrate_camera_scene(u32 operation, f32 distance = 100) noexcept;
    /// 32-point target ring on the selected 3D surface, composition pixels.
    u32 camera_track_target(i64 frame, f32* xy, u32 capacity) noexcept;
    /// Place an already-imported GLB/3D object at the selected tracked surface.
    bool place_model_on_track(u64 layer) noexcept;
    /// Point / 2 Point / Planar / Corner Pin / Stabilizer. Seeds are source
    /// pixels, analysis starts at the playhead (stabilizer uses the full clip),
    /// up to tracking::kMaxMotionTrackFrames. A cancelled job still exiting
    /// does not block a new start. Frames without a measurement stay invalid
    /// (counted in `lost`) and never block applying.
    bool start_motion_track(u64 layerId, u32 tool, u32 model, bool backward,
                            const f32* pointsXY, u32 count, f32 featureRadius = 12, f32 searchRadius = 48) noexcept;
    bool restore_motion_track(u64 layerId) noexcept;
    void cancel_motion_track() noexcept;
    struct MotionTrackStatus {
        u32 state = 0, tool = 0, frames = 0, validFrames = 0, lost = 0, reacquired = 0;
        f32 progress = 0, confidence = 0, errorPx = 0, cropPercent = 0;
        u64 memoryBytes = 0;
        std::string message;
    };
    [[nodiscard]] MotionTrackStatus motion_track_status() noexcept;
    /// Identity of the active/cached analysis in this project/composition.
    // Source LayerId, rather than the media asset ID; zero outside its context.
    [[nodiscard]] u64 motion_track_source() noexcept;
    /// x,y,confidence for the chosen source points, in composition pixels.
    u32 motion_track_features(i64 frame, f32* out3, u32 capacity) noexcept;
    /// Apply: 0 new Null, 1 target transform, 2 target Corner Pin,
    /// 3 stabilize source (Stabilizer = global motion; Point / 2 Point = the
    /// tracked detail stays still), 4 new Shape, 5 new Text. One undo
    /// operation. Lost frames get no keyframe (interpolated by the curve).
    [[nodiscard]] Result<u64> apply_motion_track(u64 target, u32 apply, bool lock = false,
        f32 smoothSeconds = .5f, f32 maxScale = 1.15f, u32 crop = 1) noexcept;
    /// Load an existing analysis without starting decoding/analysis.
    bool restore_camera_track(u64 layerId) noexcept;
    /// Interrompe a análise (libera os quadros; o projeto não muda).
    void cancel_camera_track() noexcept;
    [[nodiscard]] CameraTrackStatus camera_track_status() noexcept;
    /// Cria a câmera rastreada (keyframes por quadro, FOV resolvida) e um Nulo
    /// 3D no chão da cena (plano dominante) ou no centro dos pontos. Devolve a
    /// câmera. A câmera antiga ativa é desativada (desfazível).
    // object: 0 camera only, 1 null, 2 shape, 3 text, 4 solid.
    [[nodiscard]] Result<u64> apply_camera_track(i64 selectionFrame = -1, Vec4 selectionRect = {}, u32 object = 1) noexcept;
    /// Stable feature ids (independent of playhead and missing observations).
    u32 select_camera_track_points(const u32* ids, u32 count, u32 operation = 0) noexcept;
    /// x,y,confidence,trackId,selected,reprojectionError; solved points use the
    /// actual 3D projection, rejected tracks remain at their 2D observations.
    u32 camera_track_features_detail(i64 frame, f32* out6, u32 maxPoints) noexcept;
    /// Pontos 3D reconstruídos, no mundo da composição (depois de aplicar).
    [[nodiscard]] std::vector<Vec3> camera_track_points() noexcept;
    /// Pontos seguidos no quadro `frame` da composição, em px da composição
    /// sobre o vídeo analisado: (x, y, estado) — estado 1 = entrou no solve,
    /// 0 = rejeitado. Como os pontos coloridos do AE. Devolve quantos.
    u32 camera_track_features(i64 frame, f32* out3, u32 maxPoints) noexcept;

    // --- Gizmo 3D ----------------------------------------------------------------
    /// Setas do gizmo da camada (só camadas que vivem no espaço 3D): origem e
    /// pontas dos eixos X, Y, Z do MUNDO (comprimento `length` no mundo),
    /// projetadas em px da composição: {ox, oy, xx, xy, yx, yy, zx, zy}.
    bool query_gizmo(u64 layerId, f32 length, f32* out8, bool localSpace = false) noexcept;
    /// Position, screen-to-parent pan basis, rotation and rotation gain (13 floats).
    bool query_preview_gesture_basis(u64 layerId, f32* out13) noexcept;
    /// Posição LOCAL (espaço do pai) que leva a camada `amount` unidades do
    /// mundo ao longo do eixo `axis` (0 X, 1 Y, 2 Z) a partir de onde está.
    /// axis 0..2 uses world XYZ; 3..5 uses evaluated local XYZ. Output is parent-local position.
    bool gizmo_move_local(u64 layerId, u32 axis, f32 amount, f32* outXYZ) noexcept;
    /// Trackball do gizmo de girar (core/Trackball.hpp): origem em px da
    /// composição (2), eixos locais na vista A (9), frame pai→vista F (9) e a
    /// Rotação XYZ avaliada no cabeçote (3) — trackball::kQueryFloats floats.
    bool query_trackball(u64 layerId, f32* out23) noexcept;

    // --- Ambiente 3D (HDRI) ------------------------------------------------------
    /// HDRI Radiance (.hdr) do arquivo local: ilumina e reflete nos modelos 3D
    /// da composição atual. Devolve o asset.
    [[nodiscard]] Result<u64> import_hdri(const char* path, u64 objectLayer = 0) noexcept;
    /// Volta ao estúdio neutro.
    bool clear_hdri() noexcept;
    /// Intensidade (≥ 0) e giro (graus) do ambiente.
    bool set_environment_background(bool visible) noexcept;
    bool environment_background() noexcept;
    bool set_environment_background_range(i64 start, i64 end) noexcept;
    i64 environment_background_start() noexcept;
    i64 environment_background_end() noexcept;
    bool set_environment_params(f32 intensity, f32 rotationDeg) noexcept;
    /// {tem HDRI (0/1), intensidade, giro}.
    bool query_environment(f32* out3) noexcept;
    /// v32: ambiente de estúdio PROCEDURAL (StudioEnvironment.hpp): 1 estúdio
    /// escuro (faixas de softbox, chão preto), 2 estúdio de produto (ciclorama
    /// cinza), 3 céu com sol. Vale para a composição inteira (`objectLayer` 0,
    /// quando ela não tem HDRI importado — o HDRI importado sai) ou para UM
    /// objeto 3D. 0 = volta ao estúdio neutro / ao ambiente do projeto.
    /// Entra no desfazer e no arquivo. Devolve a chave do ambiente.
    [[nodiscard]] Result<u64> set_studio_environment(u32 preset, u64 objectLayer = 0) noexcept;
    /// Preset de estúdio da composição atual (0 = nenhum).
    [[nodiscard]] u32 studio_environment() noexcept;
    /// v32: chão do grupo 3D. `mode` 0 sem chão, 1 chão visível, 2 só sombra e
    /// reflexo (shadow catcher, compõe sobre o 2D). Cor linear; rugosidade e
    /// refletividade 0..1 (0 = sem reflexo planar); força da sombra de contato
    /// 0..1; `fade` = raio do desbotamento em raios dos modelos (1..100).
    bool set_scene_floor(u32 mode, f32 r, f32 g, f32 b, f32 roughness, f32 reflectivity,
                         f32 contactShadow = 0.8f, f32 fade = 6.0f) noexcept;
    /// {modo, r, g, b, rugosidade, refletividade, sombra de contato, fade}.
    bool query_scene_floor(f32* out8) noexcept;

    // --- Qualidade e pós do 3D (composição atual) ---------------------------------
    /// Nível de qualidade do 3D: 0 AUTO (segue o preview adaptativo), 1 BAIXO
    /// (1 amostra + FXAA), 2 MÉDIO (MSAA 2×), 3 ALTO (MSAA 4×), 4 ULTRA (4×,
    /// 8× no export, bloom na resolução inteira). O export nunca fica abaixo
    /// do ALTO. Entra no desfazer e no arquivo do projeto.
    bool set_scene3d_quality(u32 tier) noexcept;
    [[nodiscard]] u32 scene3d_quality() noexcept;
    /// Tone map do grupo 3D (0 PBR Neutral, 1 AgX fílmico) e exposição do
    /// grupo (multiplicador linear, 0,01..64; 1 = neutro).
    bool set_scene3d_tonemap(u32 op, f32 exposure) noexcept;
    /// Bloom HDR do 3D: liga, força (0..4; 0,6 = sutil) e limiar (0 = sem
    /// limiar, espalhamento conservador).
    bool set_scene3d_bloom(bool on, f32 intensity, f32 threshold) noexcept;
    /// {qualidade, operador, exposição, bloom (0/1), força, limiar}.
    bool query_scene3d_post(f32* out6) noexcept;

    /// Ambiente POR OBJETO (v22): um modelo 3D escolhe entre o ambiente do
    /// projeto (0) e o dele (1), com HDRI, intensidade, giro e exposição
    /// próprios. O estado é deste objeto — mexer aqui não toca nos outros.
    /// `hdriAsset` = 0 volta ao estúdio neutro daquele objeto.
    bool set_object_environment(u64 layerId, u32 source, u64 hdriAsset, f32 intensity, f32 rotationDeg,
                                f32 exposure) noexcept;
    /// {fonte, asset, intensidade, giro, exposição}.
    bool query_object_environment(u64 layerId, f32* out5, u64* outAsset = nullptr) noexcept;

    /// Sombras de um objeto 3D (modelo importado ou texto 3D): se ele projeta
    /// (`cast`) e se recebe (`receive`) a sombra dos outros.
    bool set_model_shadows(u64 layerId, bool cast, bool receive) noexcept;
    /// {projeta, recebe}.
    bool query_model_shadows(u64 layerId, f32* out2) noexcept;

    /// MOSTRAR INTERIOR (dupla face) de um objeto 3D (modelo, texto ou forma
    /// 3D): as faces de trás são desenhadas com a normal invertida e a mesma
    /// textura — a câmera dentro do cubo vê o lado de dentro. Grava ligado ou
    /// desligado explícito (o padrão "automático" liga só nas formas 3D
    /// prontas). Um passo de desfazer. API em EngineShape3D.cpp.
    bool set_model_interior(u64 layerId, bool on) noexcept;
    /// 1 = mostra o interior agora, 0 = não, −1 = não é objeto 3D.
    [[nodiscard]] i32 query_model_interior(u64 layerId) noexcept;

    /// MINIATURA DE MATERIAL (scene3d/MaterialPreview.hpp): a bola de estúdio
    /// do material `material` da camada 3D — modelo importado (material i),
    /// forma 3D (parte i) ou texto 3D (região i) — com os valores do instante
    /// (ajuste da camada + keyframes) e a imagem do mapa de cor. `size` ×
    /// `size` (16..256) em RGBA8 sRGB, alfa reto. Pode ser chamada fora da
    /// thread principal: a trava do modelo só cobre a cópia dos fatores; a
    /// bola é calculada na CPU, fora dela, e guardada num cache por receita.
    bool material_preview(u64 layerId, u32 material, u32 size, std::vector<u8>& outRgba) noexcept;

    /// TEXTURA DO MATERIAL (0.3.6, v52): a imagem de cor do material
    /// `material` do objeto 3D (modelo importado ou forma 3D) trocada por uma
    /// IMAGEM (arquivo) ou por uma CAMADA da mesma composição, renderizada a
    /// cada quadro (MaterialOverride::textureImage/textureLayer). Cada troca é
    /// um passo de desfazer. A camada passa por material_texture_source_ok
    /// (nunca o próprio objeto nem o que mora na cena 3D). API em EngineMedia.cpp.
    Status set_material_texture_image(u64 layerId, u32 material, const char* path) noexcept;
    Status set_material_texture_layer(u64 layerId, u32 material, u64 sourceLayer) noexcept;
    Status clear_material_texture(u64 layerId, u32 material) noexcept;
    /// 0 = o mapa do arquivo, 1 = imagem, 2 = camada. `outRef` = asset/camada
    /// empacotados; `outName` = nome do arquivo ou da camada.
    u32 query_material_texture(u64 layerId, u32 material, u64* outRef = nullptr,
                               std::string* outName = nullptr) noexcept;
    /// As camadas da composição que podem cobrir um material deste objeto, na
    /// ordem da pilha (de cima para baixo).
    std::vector<u64> material_texture_layers(u64 layerId) noexcept;
    /// A bola de um material pronto do texto 3D (0..6, a mesma ordem de
    /// apply_text3d_material_preset). Não depende do projeto.
    bool text3d_preset_preview(u32 preset, u32 size, std::vector<u8>& outRgba) noexcept;
    /// Aplica o material pronto do Texto 3D. Metal (Cromo, Ouro, Escovado, Metal
    /// cinemático) numa cena sem estúdio nem HDRI liga o "Estúdio cromado": sem
    /// ele o metal só refletia o degradê analítico e ficava chapado.
    bool apply_text3d_preset(u64 layerId, u32 preset) noexcept;

    // --- Pré-composição ----------------------------------------------------------
    /// Move as camadas para uma composição nova (mesmo tamanho, taxa e
    /// duração; fundo transparente) e põe no lugar UMA camada que a mostra, na
    /// posição da mais alta. Os tempos não mudam. Devolve a camada nova.
    [[nodiscard]] Result<u64> precompose(const u64* ids, u32 count, const char* name = nullptr) noexcept;
    /// Group and mask the composed content with the highest selected visual
    /// layer. Exclude cuts a hole; otherwise only the source alpha is retained.
    /// Preserves stack order/local timing and records one undo action. Missing,
    /// locked or nonvisual layers reject the whole selection before mutation.
    [[nodiscard]] Result<u64> group_and_mask(const u64* ids, u32 count, bool exclude,
                                           const char* name = nullptr) noexcept;
    /// A MESMA regra de `group_and_mask`, sem mudar nada: as telas habilitam
    /// "Agrupar e mascarar/excluir" por ela (≥ 2 camadas visuais destravadas).
    [[nodiscard]] bool can_group_and_mask(const u64* ids, u32 count) noexcept;
private:
    [[nodiscard]] Status group_and_mask_admission_locked(const u64* ids, u32 count) const noexcept;
    [[nodiscard]] Result<u64> precompose_locked(const u64* ids, u32 count, const char* name,
                                               bool mask, bool exclude) noexcept;
public:
    /// Desagrupa: as camadas da pré-composição voltam para a composição atual,
    /// no lugar da camada, com os mesmos tempos na tela (aparadas ao trecho que
    /// a camada mostrava). Transform da camada ≠ identidade vira um Nulo pai
    /// (a tela não muda). Recusa, com o motivo em `why`, o que mudaria o
    /// resultado: efeitos, máscaras, mistura, opacidade, 3D, tempo alterado,
    /// transições/eco na camada; câmera/luz ou fundo opaco dentro.
    /// Devolve quantas camadas voltaram.
    [[nodiscard]] Result<u32> ungroup_precomp(u64 layerId, std::string* why = nullptr) noexcept;
    /// Entra na pré-composição da camada (a timeline passa a mostrar ela).
    bool open_precomp(u64 layerId) noexcept;
    /// Volta para a composição principal. false = já estava nela.
    bool close_precomp() noexcept;
    /// Profundidade atual (0 = principal).
    [[nodiscard]] u32 precomp_depth() noexcept;
    /// Nome da composição aberta.
    [[nodiscard]] std::string current_composition_name() noexcept;

    // --- Copiar e colar -----------------------------------------------------------
    /// Session clipboard; pasting into another project imports its media and
    /// nested compositions with fresh handles.
    u32 copy_layers(const u64* ids, u32 count) noexcept;
    /// Cola no frame (o começo da mais cedo cai nele; as outras mantêm a
    /// distância). As coladas ficam escolhidas. Devolve quantas entraram.
    u32 paste_layers(i64 frame) noexcept;
    /// Estilo = mesclagem, opacidade, efeitos (trocados) e a aparência do tipo
    /// (texto: fonte/tamanho/cor/contorno; forma: preenchimento/contorno).
    bool copy_style(u64 layerId) noexcept;
    u32 paste_style(const u64* ids, u32 count) noexcept;
    /// Transform controls and their keyframes/expressions; paste preserves the
    /// destination's parent, media, effects and time range. Clipboard bit 16.
    bool copy_transform(u64 layerId) noexcept;
    u32 paste_transform(const u64* ids, u32 count) noexcept;
    /// Efeitos (com os keyframes deles): colar ACRESCENTA ao fim da pilha.
    /// Os keyframes mantêm a distância do INÍCIO da camada (tempo local; a
    /// velocidade do clipe não muda isso) e o espaçamento em segundos — numa
    /// pré-comp com outro fps os quadros são reescalados.
    u32 copy_effects(u64 layerId, u32 effectId = kInvalidIndex) noexcept;
    u32 paste_effects(const u64* ids, u32 count) noexcept;
    // --- Omega efeitos (área 07, P0-1): menu ⋮ do efeito — EngineOmegaEffects.cpp ---
    /// Duplica o efeito `effectId` da camada: parâmetros, keyframes, expressões
    /// e malha vão para uma cópia no FIM da pilha, num passo de desfazer.
    /// Devolve o id novo; kInvalidIndex = camada travada, efeito inexistente ou
    /// tipo que não se repete (remapear tempo, layout de texto/forma 3D).
    u32 duplicate_effect(u64 layerId, u32 effectId) noexcept;
    /// Cola as CONFIGURAÇÕES do efeito da área de transferência no efeito
    /// `effectId`: só quando a área de transferência tem UM efeito e do mesmo
    /// tipo. Troca parâmetros, trilhas (keyframes e expressões) e malha;
    /// mantém id, posição, ligado/desligado e o estado do painel. Um passo de
    /// desfazer. false = nada mudou.
    bool paste_effect_settings(u64 layerId, u32 effectId) noexcept;
    /// Tipo do efeito na área de transferência quando ela guarda exatamente UM
    /// efeito (o que "Colar configurações" aceita); 0 = vazia ou vários.
    [[nodiscard]] u32 clipboard_effect_type() noexcept;
    /// Keyframes no instante do frame (todas as propriedades com marca ali).
    u32 copy_keyframes(u64 layerId, i64 frame) noexcept;
    /// Packed references: property, effect ID, parameter component, local frame.
    /// Selection operations are atomic; moving cannot overwrite an unselected key.
    u32 copy_keyframe_selection(u64 layerId, const i64* references, u32 count) noexcept;
    u32 edit_keyframe_selection(u64 layerId, const i64* references, u32 count,
                               i64 delta, bool remove) noexcept;
    u32 paste_keyframes(const u64* ids, u32 count, i64 frame) noexcept;
    /// Copiar animação (app antigo): TODOS os keyframes de todas as
    /// propriedades, com o tempo relativo ao primeiro deles. `paste_keyframes`
    /// cola no cabeçote de outras camadas. Devolve quantos keyframes.
    u32 copy_animation(u64 layerId) noexcept;
    /// Otimizar keyframes: tira os redundantes sem mudar o movimento
    /// (animation/KeyframeOptimize.hpp). `property` < 0 = todas as tracks;
    /// senão só as dessa TrackProperty. `tolerance` = fração da amplitude de
    /// cada track (0.01 = 1 %). Um passo de desfazer; devolve quantos saíram.
    u32 optimize_keyframes(u64 layerId, i32 property, f32 tolerance) noexcept;
    /// Bits: 1 camadas, 2 estilo, 4 efeitos, 8 keyframes.
    [[nodiscard]] u32 clipboard_state() noexcept;

    // --- Presets (formato em project/Presets.hpp) -------------------------------
    /// Preset `kind` (Effects, Text, Animation) da camada, em JSON. Vazio = a
    /// camada não tem o que salvar desse tipo. `parts` = TextPresetParts.
    [[nodiscard]] std::string save_preset(u64 layerId, presets::PresetKind kind, const std::string& name,
                                          u32 parts = presets::kTextAll) noexcept;
    /// Preset de efeitos com SÓ o efeito `effectId` (id estável da instância
    /// na camada) e os keyframes dele. Vazio = camada ou efeito não existe.
    [[nodiscard]] std::string save_effect_preset(u64 layerId, u32 effectId, const std::string& name) noexcept;
    /// Converte um XML de projeto da referência (projeto, elemento ou preset
    /// compartilhado; também o pacote .zip/.amproj) num preset de EFEITOS do
    /// Aurea (formato de project/Presets.hpp, pronto para `apply_preset`).
    /// Vazio = nada aproveitável (`report.error` diz o porquê); o que ficou de
    /// fora vai em `report.skipped`/`report.warnings`. Não mexe no projeto.
    [[nodiscard]] std::string import_alight_motion(const std::string& data, presets::AlightImportReport& report) noexcept;
    /// Aplica o preset (um passo de desfazer). Efeitos ACRESCENTAM; texto troca
    /// estilo/animadores; animação começa no cabeçote (ou no início da camada
    /// se o cabeçote estiver fora dela) e, com `durationFrames` > 0, estica até
    /// essa duração. JSON inválido ou tipo que não serve à camada → false, sem
    /// mudar nada. `error` (opcional) diz o porquê.
    bool apply_preset(u64 layerId, const std::string& json, i64 durationFrames = 0, std::string* error = nullptr) noexcept;

    // --- After Effects (project/AeImport.hpp; EngineAeImport.cpp) -------------------
    /// Preset de animação (.ffx) ou script (.jsx) do After Effects. O tipo vem
    /// do conteúdo (RIFX/FaFX) e da extensão. O .ffx vai para `targetLayer`
    /// (os keyframes começam no cabeçote, como no AE); o .jsx roda no sandbox
    /// (sem arquivo, rede ou sistema) com `targetLayer` como camada
    /// selecionada (0 = nenhuma) e pode criar camadas. TUDO num passo de
    /// desfazer — nada mudou, nenhum passo. O relatório diz o que entrou e o
    /// que ficou de fora (`ae::report_json` é o JSON das pontes).
    [[nodiscard]] ae::ImportReport import_ae_file(const char* path, u64 targetLayer) noexcept;
    /// O mesmo com o conteúdo já lido (`fileName` só serve para a extensão).
    [[nodiscard]] ae::ImportReport import_ae_data(const std::string& data, const std::string& fileName,
                                                  u64 targetLayer) noexcept;

    // --- Modo Edição (timeline magnética) ------------------------------------------
    /// Ligar/desligar o modo Edição da COMPOSIÇÃO. Ele é o PADRÃO das camadas
    /// novas: cada camada nasce com a Linha Magnética no estado do modo. Quem
    /// já existe não muda — a opção de cada uma manda.
    void set_edit_mode(bool on) noexcept;
    [[nodiscard]] bool edit_mode() noexcept;
    /// INTERFACE DO PROJETO (0.2.5): 0 = Composição (padrão), 1 = Edição. Fica
    /// no projeto — cada projeto reabre na interface em que foi deixado — e não
    /// entra no desfazer (é um modo de tela, não uma edição). Passar para a
    /// Edição liga o ímã da composição atual e, se nenhuma linha for magnética,
    /// faz da linha de vídeo/imagem mais funda a TRILHA PRINCIPAL magnética —
    /// isso sim num passo de desfazer. Voltar à Composição não desliga o ímã:
    /// a pessoa decide no menu. false = sem projeto ou modo inválido.
    bool set_project_ui_mode(u32 mode) noexcept;
    [[nodiscard]] u32 project_ui_mode() noexcept;
    /// A TRILHA PRINCIPAL da interface de Edição: o trecho de vídeo/imagem/
    /// composição mais fundo na pilha (id empacotado). A linha dele (`trackId`)
    /// é a trilha principal; 0 = nenhuma mídia ainda.
    [[nodiscard]] u64 main_track_anchor() noexcept;
    /// "Para camada" da Edição: o corte sai da trilha principal para uma linha
    /// só dele logo ACIMA dela, solto no tempo; o buraco que ficou na trilha
    /// principal fecha. `atFrame` >= 0 também leva o corte para esse quadro (a
    /// mídia importada como camada entra no cabeçote). Um passo de desfazer.
    bool move_clip_to_overlay(u64 layerId, i64 atFrame = -1) noexcept;
    /// "Para trilha principal": o corte entra no FIM da trilha principal,
    /// magnético, logo abaixo do trecho mais alto dela. Um passo de desfazer.
    bool move_clip_to_main_track(u64 layerId) noexcept;
    /// TRANSIÇÃO NUM CORTE da trilha (o quadradinho entre dois trechos): a saída
    /// de `outgoing` e a entrada de `incoming` com o mesmo tipo (0 nenhuma, 1
    /// dissolver, 2 deslizar p/ cima, 3 deslizar da esquerda, 4 zoom, 5 girar) e
    /// a mesma duração em quadros (cada lado limitado à metade do trecho). Um
    /// passo de desfazer. O tipo do corte é o da entrada de `incoming`.
    bool set_cut_transition(u64 outgoing, u64 incoming, u32 type, u32 frames) noexcept;
    [[nodiscard]] u32 cut_transition(u64 incoming) noexcept;

    /// LINHA MAGNÉTICA da camada (um passo de desfazer). Ligada, os cortes
    /// dela se comportam como uma faixa de montagem: aparar e apagar mexem só
    /// nos vizinhos DA MESMA LINHA (mesmo zOrder) — texto, overlay e 3D em
    /// outras linhas não andam. Desligada, a camada fica solta no tempo.
    bool set_layer_magnetic_track(u64 layerId, bool on) noexcept;
    [[nodiscard]] bool layer_magnetic_track(u64 layerId) noexcept;

    /// Arrasta um trecho para outro ponto DA MESMA LINHA, com os vizinhos
    /// abrindo espaço e a fita fechando em seguida — reordenar os cortes
    /// arrastando. Só vale em linha magnética, em trecho com fonte e destravado.
    bool reorder_clip(u64 layerId, i64 targetFrame) noexcept;
    /// Arrasto VERTICAL de UM trecho (como na referência: cada fileira é
    /// uma camada) — só ele anda, nunca a linha inteira. Um passo de desfazer.
    /// `mode` 0 = fileira própria logo ACIMA da fileira de `anchorId` (acima do
    /// trecho mais alto da linha dele); `anchorId` 0 = no fundo da pilha.
    /// `mode` 1 = ENTRAR na linha de `anchorId` (logo abaixo do trecho mais
    /// alto dela) se couber no tempo sem sobrepor ninguém; não cabendo, vira
    /// uma fileira própria nesse mesmo lugar. Sair de uma linha compartilhada
    /// dá à camada uma linha só dela; numa linha MAGNÉTICA, o buraco que ela
    /// deixou fecha (como no ripple). false = recusado ou nada mudou.
    bool move_layer_to_row(u64 layerId, u64 anchorId, i32 mode) noexcept;
    /// Exclui as camadas e fecha só os buracos que a exclusão criou (um passo
    /// de desfazer). Vale em qualquer modo.
    bool ripple_delete(const u64* ids, u32 count) noexcept;
    /// Fecha todos os intervalos vazios da composição. Devolve os frames removidos.
    i64 remove_gaps() noexcept;
    /// Corta a composição no frame (duração = frame; camadas além são aparadas).
    bool trim_composition(i64 frame) noexcept;
    /// Shared NLE edits: 0 trim-in, 1 trim-out (absolute timeline frame),
    /// 2 slip, 3 roll-in, 4 roll-out, 5 slide (signed frame delta).
    /// 6 moves any layer to an absolute frame, preserving duration/source/keys.
    /// 7 extends the out-point to the remaining source handle (amount ignored).
    /// 8/9 extend in/out to an outside playhead, keeping the opposite edge
    /// and neighbouring layers fixed even when magnetic editing is enabled.
    /// Neighbours are explicit IDs; invalid/locked edits leave history untouched.
    /// Slip changes source time only. Roll/slide never ripple unrelated layers.
    bool edit_clip_time(u64 layerId, u32 operation, i64 amount, u64 previous = 0, u64 next = 0) noexcept;
    // --- Omega timeline (área 03, P1-8): alças de velocidade -----------------------
    /// Estica (ou encolhe) a camada para [newStart, newEnd) mantendo o MESMO
    /// trecho da fonte: a velocidade vira `velocidade × antiga / nova` (faixa
    /// 0,05…16×; fora dela a duração para no limite). Chaves de velocidade e de
    /// remapeamento e os keyframes da camada são reescalados no tempo local (a
    /// partir do ponto de entrada), como na referência. Linha magnética: o
    /// começo fica e os vizinhos da linha andam junto com o fim. Um passo de
    /// desfazer; camada travada ou vizinho travado no caminho = false.
    bool stretch_layer_span(u64 layerId, i64 newStart, i64 newEnd) noexcept;
    // Omega painel: (área 04) painel da camada — src/engine/EngineOmegaLayer.cpp ----
    /// Bits de `query_layer_panel` (os 5 primeiros são os de `query_clip_time_actions`).
    static constexpr u32 kLayerPanelTrimStart = 1;            ///< cabeçote dentro (s ≤ t ≤ e): trio
    static constexpr u32 kLayerPanelSplit = 2;
    static constexpr u32 kLayerPanelTrimEnd = 4;
    static constexpr u32 kLayerPanelExtendStart = 8;          ///< cabeçote antes: esticar o início (op 8)
    static constexpr u32 kLayerPanelExtendEnd = 16;           ///< cabeçote depois: esticar o fim (op 9)
    static constexpr u32 kLayerPanelMoveStart = 32;           ///< cabeçote antes: mover para começar nele
    static constexpr u32 kLayerPanelMoveEnd = 64;             ///< cabeçote depois: mover para terminar nele
    static constexpr u32 kLayerPanelRetimeExtendStart = 128;  ///< retempo (muda a velocidade), t < s
    static constexpr u32 kLayerPanelRetimeShortenStart = 256; ///< s < t < e
    static constexpr u32 kLayerPanelRetimeShortenEnd = 512;   ///< s < t < e
    static constexpr u32 kLayerPanelRetimeExtendEnd = 1024;   ///< t > e
    static constexpr u32 kLayerPanelLocked = 2048;
    static constexpr u32 kLayerPanelHasAudio = 4096;          ///< `hasAnyAudio`: vídeo, áudio, grupo com som
    static constexpr u32 kLayerPanelGainKeys = 8192;          ///< volume com mais de 1 keyframe
    static constexpr u32 kLayerPanelMissingMedia = 16384;     ///< o arquivo da fonte sumiu
    static constexpr u32 kLayerPanelSpeedBakeable = 32768;    ///< sem curva de tempo nem velocidade animada
    /// O estado do painel da camada com o cabeçote em `frame` (bits acima).
    /// Camada travada: some o mover e o retempo; o bit `Locked` avisa a UI.
    [[nodiscard]] u32 query_layer_panel(u64 layerId, i64 frame) noexcept;
    /// Move a camada inteira para COMEÇAR em `frame` ou, com `alignEnd`, para
    /// TERMINAR nele (fim exclusivo no cabeçote, como o aparar fim). Duração,
    /// fonte e keyframes andam juntos; um passo de desfazer (op 6).
    bool move_layer_to_playhead(u64 layerId, i64 frame, bool alignEnd) noexcept;
    /// Retempo: leva uma ponta até `frame` MUDANDO a velocidade e mantendo o
    /// trecho da fonte (`stretch_layer_span`). `edge`: 0 esticar o início (t < s),
    /// 1 encurtar o início (s < t < e), 2 encurtar o fim (s < t < e), 3 esticar o
    /// fim (t > e). Fora da regra ou travada = false.
    bool retime_layer_to_playhead(u64 layerId, u32 edge, i64 frame) noexcept;
    /// Velocidade constante em QUALQUER tipo (0,05…16×): a duração acompanha e
    /// a animação da própria camada (keyframes, máscaras, caminhos, malhas) é
    /// reescalada no tempo local, então forma, texto, nulo, câmera… tocam mais
    /// rápido ou mais devagar; com fonte, o trecho da fonte fica. Os passos
    /// seguidos sem outra edição no meio partem da MESMA camada de partida (um
    /// arrasto não acumula arredondamento). Recusa curva de tempo e velocidade
    /// animada (essas usam o painel de velocidade completo) e camada travada.
    bool set_layer_speed_baked(u64 layerId, f32 speed) noexcept;
    /// Contextual dock actions: trim-in=1, split=2, trim-out=4,
    /// extend-in to playhead=8, extend-out to playhead=16. At either edge
    /// the three cuts remain visible; edits still require a nonempty interval.
    [[nodiscard]] u32 query_clip_time_actions(u64 layerId, i64 frame) noexcept;

    // --- Omega aparência (área 05) — src/engine/EngineOmegaAppearance.cpp ----------
    // Borda e sombra (P0-4): DECORAÇÕES = efeitos existentes com papel fixo
    // (EffectInstance::role), escondidos da lista de Efeitos e sempre no fim da
    // pilha (traço → bordas → sombra). Remover/ligar/parâmetros usam os
    // comandos de efeito de sempre (EffectRemove, EffectSetEnabled, …).
    /// Põe uma decoração (`role` = kEffectRoleBorder ou kEffectRoleShadow):
    /// borda nova = Contorno da silhueta DENTRO, branco, 6 px, no fim das
    /// bordas; sombra = Sombra projetada preta, deslocamento (5, 5) px (uma por
    /// camada: já existindo, devolve a dela e a liga). Um passo de desfazer.
    /// Devolve o id do efeito; kInvalidIndex = camada travada/sem lugar.
    u32 decoration_add(u64 layerId, u32 role) noexcept;
    /// Reordena uma decoração DENTRO do grupo do papel dela (`slot` 0 = a
    /// primeira borda). Um passo de desfazer. false = nada mudou.
    bool decoration_move(u64 layerId, u32 effectId, u32 slot) noexcept;
    /// Deslocamento X/Y (px) da sombra → direção (graus) e distância do efeito
    /// (sem parâmetro novo de shader). Animados: keyframe no cabeçote. Um passo.
    bool decoration_set_shadow_offset(u64 layerId, u32 effectId, f32 dx, f32 dy) noexcept;
    /// O deslocamento X/Y (px) da sombra no cabeçote (a conta inversa). false =
    /// não é uma sombra desta camada.
    bool decoration_shadow_offset(u64 layerId, u32 effectId, f32& dx, f32& dy) noexcept;

    // Cor e preenchimento (P0-3 / P1-4): forma SDF (não vetorial).
    static constexpr u32 kShapeFillFloats = 24;
    /// O preenchimento no cabeçote: [0] tipo (0 cor, 1 degradê), [1] degradê
    /// (0 linear, 1 radial, 2 angular), [2] preenchido (0/1), [3..6] cor RGBA,
    /// [7..10] cor do início, [11..14] cor do fim, [15..16] ponta inicial,
    /// [17..18] ponta final (relativas à caixa), [19] grupos com keyframe,
    /// [20] grupos com keyframe no cabeçote (bits: timeline/ShapeFill.hpp),
    /// [21] 1 = forma que aceita preenchimento, [22..23] tamanho da caixa (px).
    /// Cores em sRGB reto 0..1. Devolve kShapeFillFloats ou 0.
    u32 query_shape_fill(u64 layerId, f32* out, u32 capacity) noexcept;
    /// Tipo do preenchimento: 0 nenhum (filled = false), 1 cor sólida, 2 degradê
    /// (a primeira vez parte da cor atual). Um passo de desfazer.
    bool set_shape_fill_mode(u64 layerId, u32 mode) noexcept;
    /// 0 linear, 1 radial, 2 angular. Um passo de desfazer.
    bool set_shape_gradient_type(u64 layerId, u32 type) noexcept;
    /// Valor de um grupo (shape_fill::kGroup*: 0 cor, 1 cor do início, 2 cor do
    /// fim, 3/4 pontas): `count` = 4 (cor) ou 2 (ponta). Com keyframe no grupo,
    /// grava/edita o keyframe do cabeçote; senão o valor parado. Cada chamada
    /// abre um passo de desfazer — um arrasto vai dentro de um grupo da UI.
    bool set_shape_fill_value(u64 layerId, u32 group, const f32* values, u32 count) noexcept;
    /// ◆ do grupo: keyframe no cabeçote em todos os componentes ↔ tira. Um passo.
    bool toggle_shape_fill_key(u64 layerId, u32 group) noexcept;

    // Ímã do trackpad e das réguas (P1-2; contas em core/SnapMath.hpp).
    /// Alvos no cabeçote. `axis` 0/1: valores da POSIÇÃO X/Y (espaço do pai) em
    /// que uma borda ou o centro da caixa da camada encosta numa borda/centro
    /// de outra camada visível ou da composição; `guides` (opcional) recebe a
    /// linha-guia de cada alvo em px da composição. `axis` 2/3: larguras/
    /// alturas (px da composição) das outras camadas e da composição (marcas
    /// das réguas de Escala). Ordenados e sem repetidos. Camada em perspectiva
    /// ou pai girado: 0 alvos.
    u32 query_snap_targets(u64 layerId, u32 axis, f32* values, f32* guides, u32 capacity) noexcept;

    // --- Omega chrome (área 02): moldura do editor — EngineOmegaChrome.cpp ---------
    /// Trilha de grupos: os nomes das composições do caminho aberto, da
    /// principal até a atual, separados por U+001F (o item 0 é a principal).
    [[nodiscard]] std::string composition_trail() noexcept;
    /// Setas do transporte (referência): além dos marcadores e dos keyframes
    /// visíveis da camada (`query_timeline_navigation`), o começo (0), o último
    /// quadro e, com camada, o início e o fim dela. Estrito na direção; false =
    /// nada naquela direção (a UI fica parada).
    bool query_transport_navigation(u64 layerId, i64 compositionFrame, i32 direction,
                                    i64& targetFrame) noexcept;
    /// Barra inferior da seleção múltipla com o cabeçote em `frame`: 0 = cortes
    /// (aparar início · dividir · aparar fim), 1 = todas começam no cabeçote ou
    /// depois (estender/levar o início), 2 = todas acabam no cabeçote ou antes
    /// (estender/levar o fim).
    [[nodiscard]] u32 query_multi_time_actions(const u64* ids, u32 count, i64 frame) noexcept;
    /// Partes do "Colar estilo" (bits estáveis, os mesmos nas duas plataformas).
    static constexpr u32 StyleColorFill = 1;      ///< cor e preenchimento (forma, texto)
    static constexpr u32 StyleBorderShadow = 2;   ///< contorno/sombra e os efeitos de borda e sombra
    static constexpr u32 StyleBlendOpacity = 4;   ///< mesclagem e opacidade
    static constexpr u32 StyleTransform = 8;      ///< mover e transformar (sem a opacidade)
    static constexpr u32 StyleText = 16;          ///< fonte, tamanho, alinhamento, espaçamento
    static constexpr u32 StyleVolume = 32;        ///< volume, pan e fades (vídeo, áudio)
    static constexpr u32 StyleEffects = 64;       ///< os outros efeitos
    static constexpr u32 StyleSpeed = 128;        ///< velocidade (vídeo, áudio, grupo)
    /// O que o estilo copiado (`copy_style`) tem para colar, pelo tipo da camada
    /// copiada. 0 = nada copiado.
    [[nodiscard]] u32 copied_style_parts() noexcept;
    /// Cola só as `parts` pedidas nas camadas destravadas, num passo de desfazer.
    /// Parte que não serve ao tipo de destino é pulada. Devolve quantas mudaram.
    u32 paste_style_parts(const u64* ids, u32 count, u32 parts) noexcept;
    /// Modos de visão da prévia (coluna à direita do preview). NUNCA entram no
    /// export. `selectionView`: 0 normal, 1 só as escolhidas (as outras não são
    /// desenhadas; sem ids = normal), 2 as escolhidas com opacidade 0,5 fixa (as
    /// outras normais). `pixelPreview`: a composição na resolução dela e a saída
    /// por vizinho mais próximo. Mudar redesenha (o quadro guardado é descartado).
    void set_preview_view(u32 selectionView, const u64* ids, u32 count, bool pixelPreview) noexcept;
    /// O que vale agora: modo (bits 0..7), pixels (bit 8) e quantos ids (bits 16+).
    [[nodiscard]] u32 preview_view_mode() noexcept;
    static constexpr u32 kOmegaViewMaxIds = 64;

    // --- Marcas e batidas -------------------------------------------------------
    /// Liga/desliga a marca da pessoa no frame (toggle). true = ficou marcada.
    /// Tocando, vira `mark_beat_live` (o quadro do relógio mestre, sem alternar):
    /// marcar NUNCA pausa, faz seek nem recomeça o som/vídeo — mesmo quando a
    /// UI ainda acha que está parado (o status chega atrasado).
    bool toggle_marker(i64 frame) noexcept;
    /// Tap de batida ao ouvir a música: marca o instante que está SOANDO
    /// (relógio mestre do áudio quando tocando; o cabeçote quando parado).
    /// Nunca pausa, nunca alterna: um 2º toque no mesmo quadro não apaga.
    /// Devolve o quadro marcado, ou -1 se já havia marca ali / sem composição.
    i64 mark_beat_live() noexcept;
    bool move_marker(i64 from, i64 to) noexcept;
    /// from < 0 inserts; otherwise updates the existing marker atomically.
    /// Rejects occupied destinations and invalid frames without changing history.
    bool edit_marker(i64 from, i64 to, u32 color, const std::string& label) noexcept;
    bool delete_marker(i64 frame) noexcept;
    std::string marker_label(i64 frame) noexcept;
    /// Marcas da composição atual: frame, cor e tipo intercalados em `out`
    /// (3 por marca). Devolve o total (pode passar de `capacity`).
    u32 query_markers(i64* out, u32 capacity) noexcept;
    /// Detecta as batidas do som da camada e troca as marcas de batida dentro
    /// do trecho dela. Síncrono (decodifica o áudio): chamar fora da thread de
    /// UI. Devolve o número de batidas; `bpm` recebe o tempo.
    [[nodiscard]] Result<u32> detect_beats(u64 layerId, f64* bpm = nullptr) noexcept;
    /// Dados de texto da camada (para a UI editar). false = não é texto.
    bool query_text(u64 layerId, TextData& out) noexcept;
    /// Imagem já decodificada pela plataforma (RGBA8 sRGB, alfa reto).
    /// `sourcePath` (URI/caminho) fica no projeto: ao reabrir, o motor pede a
    /// imagem de novo ao `imageLoader`. Sem origem, a imagem só vive na sessão.
    /// `orientation` = EXIF 1..8 dos pixels CRUS: o motor gira, limita a foto a
    /// 4× a área da composição e, se ainda não couber no espaço de imagens do
    /// projeto, reduz (mesma proporção, nunca abaixo de image::kShrinkFloorSide)
    /// em vez de recusar (ImageResample.hpp).
    [[nodiscard]] Result<u64> import_image(const u8* rgba, u32 width, u32 height,
                                           const char* name, const char* sourcePath = nullptr,
                                           u8 orientation = 1) noexcept;
    /// Importa um modelo 3D e cria a layer no topo, enquadrada. O parse roda
    /// na thread de quem chama (a plataforma chama fora da UI); o modelo só
    /// trava no fim, para criar asset e layer. Falha = erro específico
    /// (`detail` preenchido), nunca "importado" com a tela preta.
    [[nodiscard]] Result<u64> import_model(const ModelImport& request, scene3d::ImportProgress* progress = nullptr,
                                           std::string* detail = nullptr) noexcept;
    /// Antes do import: custo do arquivo (só cabeçalhos/contagens) e o que
    /// cabe neste aparelho por qualidade. A UI oferece "Otimizar modelo"
    /// quando `heavy`, e recusa sem tentar quando `tooHeavy`.
    [[nodiscard]] scene3d::ModelPlan inspect_model(const std::string& path,
                                                   const scene3d::DeviceMemoryHint& memory = {}) const noexcept;
    /// O último `import_model` bem-sucedido (triângulos antes/depois).
    [[nodiscard]] ModelImportReport last_model_import() const noexcept;
    /// Asset 3D carregado (nulo = ausente/ilegível). Compartilhado: a layer
    /// apagada não invalida quem ainda desenha.
    [[nodiscard]] std::shared_ptr<const scene3d::SceneAsset> model_asset(u64 assetId) const noexcept;
    /// Texturas (e o .mtl do OBJ) que o modelo 3D da layer referencia e que
    /// não estão na pasta dele — só o nome do arquivo. Vazio = nada faltando
    /// (ou a layer não é um modelo importado).
    [[nodiscard]] std::vector<std::string> model_missing_textures(u64 layerId) noexcept;
    /// Pasta (absoluta, com a barra no fim) do arquivo do modelo 3D da layer:
    /// é para lá que a plataforma copia as texturas escolhidas depois. Vazio =
    /// a layer não é um modelo importado (texto 3D, outra camada).
    [[nodiscard]] std::string model_folder(u64 layerId) noexcept;
    /// Relê o modelo da layer do MESMO arquivo, depois que a plataforma copiou
    /// texturas para a pasta dele. O modelo relido entra como asset novo (a
    /// GPU sobe as texturas de novo) e toda layer que usava o antigo passa a
    /// usar o novo; desfazer volta ao anterior. Devolve quantos arquivos
    /// ainda faltam.
    [[nodiscard]] Result<u32> reload_model_textures(u64 layerId, std::string* detail = nullptr) noexcept;

    /// Texto 3D: malha extrudada dos contornos da fonte, numa camada de
    /// modelo 3D centrada. Devolve a layer.
    [[nodiscard]] Result<u64> add_text3d(const scene3d::Text3DSpec& spec) noexcept;
    /// Troca texto/profundidade/cor/alinhamento (a malha é gerada de novo; desfazível).
    Status set_text3d(u64 layerId, const scene3d::Text3DSpec& spec) noexcept;
    Status set_text3d_texture(u64 layerId, const std::string& path) noexcept;
    /// Receita do texto 3D da camada (falso = não é texto 3D).
    bool query_text3d(u64 layerId, scene3d::Text3DSpec& out) noexcept;
    /// Errc da última edição de texto 3D (set_text3d e quem passa por ela;
    /// 0 = aplicada). As pontes devolvem só sim/não: a UI lê o motivo daqui
    /// para dizer por que as configurações não mudaram (bug 0.3.6).
    [[nodiscard]] i32 last_text3d_status() const noexcept { return lastText3dStatus_.load(std::memory_order_relaxed); }

    // --- Formas 3D (scene3d/Shape3D.hpp; API em EngineShape3D.cpp) --------------
    /// Nova camada com a forma `kind` (Shape3DKind), centrada, uma cor por
    /// parte. `name` = nome da camada (a UI manda traduzido). Devolve a layer.
    [[nodiscard]] Result<u64> add_shape3d(u32 kind, const char* name = nullptr) noexcept;
    /// Troca a receita (cor/imagem por parte). A malha é gerada de novo; desfazível.
    Status set_shape3d(u64 layerId, const scene3d::Shape3DSpec& spec) noexcept;
    /// Receita da forma 3D da camada (falso = não é forma 3D).
    bool query_shape3d(u64 layerId, scene3d::Shape3DSpec& out) noexcept;
    /// Cor (`rgba` sRGB; nulo = mantém) e/ou imagem (`image` = arquivo; "" tira;
    /// nulo = mantém) da parte `part` (−1 = todas). Um passo de desfazer.
    Status set_shape3d_part_style(u64 layerId, i32 part, const f32* rgba, const char* image) noexcept;
    /// Partes no cabeçote, kShapePartFloats por parte: [0..8] canais (posição
    /// XYZ em unidades da forma, Y para cima; rotação XYZ em graus; escala
    /// XYZ), [9] bits dos canais com keyframe, [10] bits dos canais com key
    /// no cabeçote, [11..12] centro da parte em px da composição (como o palco
    /// mostra), [13] 1 = centro na frente da câmera. Devolve os floats
    /// precisos (0 = não é forma 3D); com `capacity` menor não escreve nada.
    static constexpr u32 kShapePartFloats = 14;
    u32 query_shape3d_parts(u64 layerId, f32* out, u32 capacity) noexcept;
    /// Grava os canais `mask` (bit c = canal c de `values9`) da parte. Canal
    /// com keyframes grava a key no cabeçote; sem keyframe, o valor parado.
    /// `continuing` = o mesmo arrasto (um passo de desfazer).
    bool set_shape3d_part(u64 layerId, u32 part, const f32* values9, u32 mask, bool continuing) noexcept;
    /// Losango da parte: com key no cabeçote tira as 9; senão marca as 9 com
    /// os valores atuais. 1 = marcou, 0 = tirou, −1 = não deu.
    i32 toggle_shape3d_part_key(u64 layerId, u32 part) noexcept;
    /// Volta a parte (−1 = todas) ao lugar: apaga as trilhas dela. Desfazível.
    bool reset_shape3d_part(u64 layerId, i32 part) noexcept;
    /// Gizmo da PARTE, no formato do `query_gizmo` (origem no centro da parte).
    bool query_shape3d_part_gizmo(u64 layerId, u32 part, f32 length, f32* out8, bool localSpace = false) noexcept;
    /// Posição da parte (unidades da forma) que a leva `amount` unidades do
    /// mundo no eixo (0..2 mundo, 3..5 os da parte). Só calcula; grava-se com
    /// `set_shape3d_part`.
    bool shape3d_part_move(u64 layerId, u32 part, u32 axis, f32 amount, f32* outXYZ) noexcept;
    /// DIVIDIR o cubo (ou uma fatia dele) em `count` fatias iguais
    /// (kShape3DSplitMin..Max) ao longo do eixo `axis` (0 X, 1 Y, 2 Z da
    /// forma). A camada vira um NULO 3D (mesmo id, nome, tempo, pai e todo o
    /// transform/keyframes dela: mover o grupo continua igual) e cada fatia é
    /// uma camada de forma 3D filha dele, com a fatia da imagem de cada face,
    /// o pivô no próprio centro (girar uma fatia gira em volta dela) e a
    /// opacidade/efeitos/animadores/trilhas das faces da camada original.
    /// O quadro fica idêntico até alguém mexer numa fatia. Um passo de
    /// desfazer. Devolve o id do nulo; `slices` (opcional) recebe as fatias na
    /// ordem do eixo.
    [[nodiscard]] Result<u64> split_shape3d(u64 layerId, u32 axis, u32 count, std::vector<u64>* slices = nullptr) noexcept;

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
    [[nodiscard]] Status render_offscreen(TextureHandle target, u32 width, u32 height, bool asPreview = false,
                                          u64 expectedGpuGeneration = 0) noexcept;

    /// Medição do último `render_offscreen` (suíte de benchmark da Fase 8A).
    /// Tudo cronometrado de verdade; a GPU só com os timers ligados
    /// (`set_offscreen_timers`) — sem eles `gpuMeasured` fica falso e `gpuMs` 0,
    /// nunca "instantâneo". O quadro é o que acabou de ser desenhado (o
    /// `render_offscreen` espera a GPU, então os timestamps já são dele).
    struct OffscreenMeasure {
        f32  prepareMs = 0.0f;     ///< CPU: prepare sob o lock (a tentativa que valeu)
        f32  mediaWaitMs = 0.0f;   ///< CPU parada esperando os quadros exatos do decoder
        u32  mediaAttempts = 0;    ///< prepares até ter todos os quadros (1 = já estavam)
        f32  recordMs = 0.0f;      ///< CPU: gravação do FrameGraph
        f32  submitMs = 0.0f;      ///< CPU: end_frame (submissão)
        f32  gpuWaitMs = 0.0f;     ///< CPU parada no wait_idle
        f32  gpuMs = 0.0f;         ///< timestamp início → fim do quadro
        bool gpuMeasured = false;
        u32  gpuPasses = 0;        ///< passes com timestamp (last_offscreen_gpu_passes)
        u32  passesExecuted = 0;
        u32  passesCulled = 0;
        u32  drawCalls = 0;        ///< contagem do renderer 2D (camadas + passes)
        u32  layersRendered = 0;
        u32  draws3D = 0;          ///< SceneStats do quadro
        u32  triangles3D = 0;
        u32  culled3D = 0;
        u32  particles = 0;        ///< slots de partícula desenhados
        u32  activeEffects = 0;    ///< efeitos vivos no plano (neutros saem)
        u64  transientBytes = 0;   ///< físicas do FrameGraph neste quadro
        u64  gpuUsedBytes = 0;     ///< alocador do backend depois do quadro
    };
    /// Liga as timestamp queries no render_offscreen (o export e as capturas
    /// continuam sem elas; é só para medir).
    void set_offscreen_timers(bool on) noexcept { offscreenTimers_ = on; }
    [[nodiscard]] OffscreenMeasure last_offscreen_measure() const noexcept { return offscreenMeasure_; }
    /// Tempos por passe do último render_offscreen medido (rótulo estático).
    u32 last_offscreen_gpu_passes(GpuTiming* out, u32 capacity) const noexcept;
    /// Estado térmico do aparelho (PowerManager no Android): o preview reduz
    /// as operações caras sob calor; o export não muda.
    void set_thermal(u32 level, bool throttling) noexcept;
    /// Fração de custo do preview (1 = completo): o menor entre o piso
    /// térmico do instante e o degrau de reduções do AUTO 2.0.
    [[nodiscard]] f32 preview_heavy_scale() const noexcept;
    /// Degrau de reduções que o estado térmico impõe (0 normal, 1 quente, 2 crítico).
    [[nodiscard]] u32 thermal_heavy_level() const noexcept;
    /// 0.4.0 "Toque leve": um gesto está aberto e o preview desenha rascunho.
    [[nodiscard]] bool interaction_draft_active() const noexcept {
        return interactionDepth_.load(std::memory_order_acquire) > 0
            && interaction_draft(caps_.tier()).heavyLevel > 0;
    }

    // --- Proxy de edição ("Otimizar vídeos pesados", 0.2.5; EngineProxy.cpp) --
    /// ProxyMode: 0 Automático, 1 Sempre, 2 Desligado. O app grava a escolha e
    /// manda na abertura; trocar põe na fila do projeto o que passou a valer.
    void set_proxy_mode(u32 mode) noexcept;
    [[nodiscard]] u32 proxy_mode() const noexcept;
    /// Proxy do vídeo da camada (estado e progresso). Sem vídeo: None.
    [[nodiscard]] ProxyStatus proxy_status(u64 layerId) noexcept;
    /// Pedidos desta sessão de projeto: o selo "Otimizando 42%".
    [[nodiscard]] ProxySummary proxy_summary() noexcept;
    /// Gera de novo (descarta o arquivo) / para de otimizar o vídeo da camada.
    bool regenerate_proxy(u64 layerId) noexcept;
    bool cancel_proxy(u64 layerId) noexcept;
    /// Mídia otimizada em disco (bytes) / apaga tudo e devolve os liberados.
    /// O export continua no original de qualquer jeito.
    [[nodiscard]] u64 proxy_cache_bytes() noexcept;
    u64 clear_proxy_cache() noexcept;

    /// O frame do playhead em RGBA8 sRGB (alfa reto), com o lado maior em
    /// `maxDim`. Captura exata, com qualidade final. Síncrono (espera a GPU).
    [[nodiscard]] Status capture_frame_rgba(u32 maxDim, std::vector<u8>& out, u32& width, u32& height) noexcept;
    /// Capa da Home: políticas da prévia e intermediários na escala da capa.
    /// A captura exata e a exportação mantêm a qualidade final.
    [[nodiscard]] Status capture_preview_frame_rgba(u32 maxDim, std::vector<u8>& out, u32& width, u32& height) noexcept;

    // --- Correção de cor no modo de edição (0.3.0, EngineColor.cpp) ---------
    /// A "Correção de cor" da camada com o papel `role` (1 Filtro, 2 Ajustar):
    /// a que existe ou uma nova (o Filtro antes do Ajustar). Devolve o id do
    /// efeito; kInvalidIndex = falhou (camada travada, cheia, papel inválido).
    u32 ensure_color_grade(u64 layerId, u32 role) noexcept;
    /// O look `look` (índice dos presets da Correção de cor; −1 = nenhum) na
    /// instância `effectId`: parâmetros e chaves voltam ao padrão (menos papel
    /// e intensidade), o look é escrito e anotado. Um passo de desfazer.
    bool apply_color_look(u64 layerId, u32 effectId, i32 look) noexcept;
    /// Todos os parâmetros da Correção de cor ao padrão (menos papel e intensidade).
    bool reset_color_grade(u64 layerId, u32 effectId) noexcept;
    /// Miniaturas dos looks sobre o quadro do cabeçote: `count` = 1 + nº de
    /// looks (a 0 é o quadro sem look), `width`×`height` RGBA8 sRGB cada, em sequência.
    [[nodiscard]] Status render_color_looks(u32 maxDim, std::vector<u8>& out, u32& width, u32& height,
                                            u32& count) noexcept;
    /// Scopes do quadro do cabeçote (a prévia reduzida a `maxDim`), em valores
    /// de tela: histograma 4×256 (R, G, B, luz), forma de onda colunas×128,
    /// parade 3×colunas×128 e vetorscópio 128×128 (Cb na horizontal, Cr na vertical).
    struct Scopes {
        u32 width = 0, height = 0, columns = 0;
        std::vector<u32> histogram;
        std::vector<u16> waveform;
        std::vector<u16> parade;
        std::vector<u16> vectorscope;
    };
    [[nodiscard]] Status compute_scopes(u32 maxDim, Scopes& out) noexcept;

    // =========================================================================
    // Plugins de efeito (docs/plugins/README.md)
    //
    // Um plugin acrescenta UM efeito ao catálogo, na categoria "Plugins", com a
    // chave `plugin.<id>`: o projeto o salva como um embutido. Sem o plugin, a
    // instância e os valores ficam no projeto (efeito desconhecido, não
    // desenha); instalar de novo o faz voltar.
    // =========================================================================
    /// Instala um `.aureaplugin` (ZIP com plugin.json) ou um `.fs` (ISF 2.0).
    /// Confere, compila (glslang + o backend ativo), desenha um quadro de teste
    /// em 256×256 (teto de 250 ms) e registra. Mesmo id com versão igual ou
    /// mais nova substitui. Devolve o id; na recusa, `status().detail()` traz a
    /// mensagem para a pessoa (com arquivo:linha nos erros de shader).
    [[nodiscard]] Result<std::string> install_plugin(const char* path) noexcept;
    /// Os plugins da pasta, com nomes/descrições por idioma, a chave do efeito
    /// e, nos que não carregaram, o motivo.
    [[nodiscard]] std::vector<plugins::PluginInfo> list_plugins() const;
    /// `list_plugins` em JSON (o formato das pontes; docs/plugins/README.md).
    [[nodiscard]] std::string plugins_json() const;
    /// Desinstala (efeito sai do catálogo, a pasta é apagada). Falso = não havia.
    bool remove_plugin(const char* id) noexcept;
    /// Troca a pasta de plugins e carrega o que houver nela (a ponte do iOS
    /// chama depois de subir o motor). Vazio = sem plugins.
    void set_plugin_directory(const char* path) noexcept;
    [[nodiscard]] std::string plugin_directory() const;

    // 0.3.4 plugins: interface, scripts e APIs (src/engine/EnginePluginsV2.cpp) --
    // Plugins do formato 2 trazem painéis/ações declarados e um script
    // (AureaScript) com a API de objetos do AUREA no estilo do After Effects.
    // As chamadas BLOQUEIAM (o script roda na thread de quem chama): a
    // interface chama de uma thread de trabalho com pilha grande. Toda
    // resposta é JSON (docs/plugins/README.md, "API do motor").
    /// Lê um pacote sem instalar (a folha de consentimento mostra o que ele pede).
    [[nodiscard]] std::string inspect_plugin(const char* path) noexcept;
    /// Liga/desliga a interface e o script do plugin (o efeito, se houver, fica).
    bool set_plugin_enabled(const char* id, bool on) noexcept;
    /// Permissões concedidas pela pessoa (plugins::kPerm*).
    bool set_plugin_permissions(const char* id, u32 granted) noexcept;
    /// Chama a função `function` do script com `argsJson` (lista JSON). Uma ação
    /// de desfazer por chamada. `locale` = idioma do app ("pt-BR", "en"...).
    [[nodiscard]] std::string plugin_call(const char* id, const char* function, const char* argsJson, const char* locale) noexcept;
    /// A pessoa mexeu num controle (`kind` = "click" | "change" | "select").
    [[nodiscard]] std::string plugin_event(const char* id, const char* control, const char* kind, const char* valueJson,
                                           const char* locale) noexcept;
    /// O painel abriu (chama o onOpen dele).
    [[nodiscard]] std::string plugin_open_panel(const char* id, const char* panel, const char* locale) noexcept;
    /// Estado dos controles (vale durante uma chamada: progresso).
    [[nodiscard]] std::string plugin_state(const char* id) noexcept;
    [[nodiscard]] std::string plugin_console(const char* id) noexcept;
    void plugin_clear_console(const char* id) noexcept;
    /// Para a chamada em andamento do plugin (qualquer thread).
    void plugin_cancel(const char* id) noexcept;
    /// Recarrega o script (depois de um erro na carga).
    void plugin_reload(const char* id) noexcept;
    /// Rede, área de transferência e seletor de arquivos da plataforma. O motor
    /// confere a permissão e o domínio antes de chamar.
    void set_plugin_host_io(plugins::HostIO io) noexcept;

    /// A PRÉVIA DE UM EFEITO (Fase 7.3): o efeito, com os valores padrão, sobre
    /// a cartela de demonstração. RGBA8 sRGB de alfa reto. Não depende de
    /// projeto nem de composição aberta — o navegador de efeitos existe antes
    /// de qualquer camada. `NotImplemented` = efeito sem prévia de um quadro.
    /// Foto de base das prévias de efeito (RGBA8 sRGB). O app manda uma vez.
    bool set_effect_preview_source(const u8* rgba, u32 width, u32 height) noexcept;
    [[nodiscard]] Status render_effect_preview(u32 typeId, u32 width, u32 height, std::vector<u8>& out,
                                               u32& outWidth, u32& outHeight) noexcept;
    /// PRÉVIA ANIMADA de um efeito: `frames` (12..16) quadros antes/depois,
    /// concatenados em `out` (RGBA8 sRGB de alfa reto, `outWidth`×`outHeight`
    /// cada), sobre a cena `scene` (EffectPreviewScene: 0 foto, 1 cartela,
    /// 2 formas, 3 baixa luz). Mesmo teto de lado da prévia estática. `Busy` =
    /// o preview do editor está com a GPU (a UI tenta de novo com recuo); o
    /// mutex de render é solto entre os quadros.
    [[nodiscard]] Status render_effect_preview_sequence(u32 typeId, u32 width, u32 height, u32 frames, u32 scene,
                                                        std::vector<u8>& out, u32& outWidth, u32& outHeight,
                                                        u32& outFrames) noexcept;

    [[nodiscard]] EngineStatus read_status() noexcept;
    [[nodiscard]] EngineTelemetry read_telemetry() noexcept;
    /// Complete preview-cache intervals for the current composition. Absolute
    /// [start, end) frame pairs; returns pairs copied (at most 30), without a GPU wait.
    [[nodiscard]] u32 copy_preview_buffer_ranges(i64* outPairs, u32 capacityRanges) noexcept;
    /// Bits: depth working=1, foreground working=2, depth failed=4, foreground failed=8.
    [[nodiscard]] u32 local_ai_status() noexcept;

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
    struct KeyframeNavigation {
        u32 visibleCount = 0;
        u32 flags = 0;           // 1 key here, 2 previous, 4 next, 8 easing segment
        i64 localFrame = 0;
        i64 previousFrame = 0;   // composition frame; valid only with flag 2
        i64 nextFrame = 0;       // composition frame; valid only with flag 4
        i64 segmentStart = 0;    // local frame; valid only with flag 8
        i64 segmentEnd = 0;      // local frame; valid only with flag 8
    };
    /// Visible keys on one track, with the same inclusive clip-end anchor rule
    /// as query_keyframes. Previous/next are strict. A segment exists only
    /// inside the visible key span; its last endpoint selects the prior segment.
    bool query_keyframe_navigation(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex,
                                   i64 compositionFrame, KeyframeNavigation& out) noexcept;
    /// Transport arrows choose the strict nearest event in composition time.
    /// With layerId=0 (or a stale id), only composition markers participate.
    /// A valid layer adds all its visible keys, including the clip-end anchor.
    /// False means there is no event in that direction; the UI stays put.
    bool query_timeline_navigation(u64 layerId, i64 compositionFrame, i32 direction,
                                   i64& targetFrame) noexcept;
    /// Seek a transport event within the composition, even after the last clip.
    /// Regular scrubbing/seek and content-bounded playback retain their limits.
    bool seek_timeline_event(i64 compositionFrame) noexcept;
    /// Keyframes de TODAS as camadas da composição atual numa consulta só (a
    /// timeline relê a cada `modelRevision`: um travamento do modelo e uma
    /// travessia de JNI em vez de uma por camada). Ordem de query_layers
    /// (frente → fundo); `outIndex[i]` diz de que camada e quantos, e as
    /// linhas vêm concatenadas em `out`. Devolve o TOTAL de keyframes e põe em
    /// `outLayers` o total de camadas; se não couber (total > capacity ou
    /// camadas > layerCapacity) nada é escrito e a UI cresce os buffers.
    u32 query_all_keyframes(bridge::KeyframeIndexRow* outIndex, u32 layerCapacity,
                            bridge::KeyframeRow* out, u32 capacity, u32* outLayers) noexcept;
    /// Miniatura da layer no instante `timelineFrame` (RGBA8 sRGB, `height`
    /// linhas). Devolve os bytes escritos, ou 0 se ainda não está pronta (o
    /// pedido fica na fila; `thumbnailGeneration` no status avisa quando
    /// chegar). `outWidth` recebe a largura.
    u32 query_thumbnail(u64 layerId, i32 timelineFrame, u32 height, u8* out, u32 capacity,
                        u32* outWidth) noexcept;

    /// Composição atual: id empacotado e ajustes (tamanho, fps, duração em
    /// frames, fundo RGBA linear). false = sem projeto.
    [[nodiscard]] i64 query_export_duration(bool trimToContent = true) noexcept;
    /// O projeto usa motion blur (composição ligada e alguma camada com ele,
    /// em qualquer composição): a tela só fala de "menos amostras" se usa.
    [[nodiscard]] bool query_export_uses_motion_blur() noexcept;
    bool query_composition(u64& id, u32& width, u32& height, f64& fps, i64& durationFrames,
                           f32 background[4]) noexcept;
    /// Teto de tamanho de composição do aparelho, como lado maior × lado
    /// menor (a mesma regra com que CompositionSetSize recusa).
    void composition_size_cap(u32& longSide, u32& shortSide) const noexcept;

    /// Detalhe de uma camada no playhead. false = camada não existe.
    bool query_layer_detail(u64 layerId, bridge::LayerDetailPOD& out) noexcept;
private:
    /// Caller holds modelMutex_. Sources retained for undo share this CPU quota.
    bool apply_motion_blur_settings_locked(Composition& comp, const MotionBlurSettings& settings) noexcept;
    [[nodiscard]] u64 source_asset_room_locked() const noexcept;
    /// Acquire BEFORE modelMutex_; slow imports must not overlap their peaks.
    std::recursive_mutex sourceImportMutex_;
    Status ensure_text3d_layout(Layer& layer) noexcept; // modelMutex_ held; caller owns undo.
    Status set_text3d_impl(u64 layerId, const scene3d::Text3DSpec& spec) noexcept;
    std::atomic<i32> lastText3dStatus_{0};
    /// Cada edição de texto 3D guarda uma malha nova e a anterior fica para o
    /// desfazer. Sem espaço: solta as malhas de texto 3D que nenhuma camada (nem
    /// a área de transferência) usa — a receita continua no asset e
    /// restore_text3d_models_locked as refaz se o desfazer/refazer religar.
    /// modelMutex_ held. Devolve quantas soltou.
    u32 release_unused_text3d_models_locked() noexcept;
    /// Refaz da receita a malha de texto 3D que uma camada usa e que não está
    /// em memória (solta acima). modelMutex_ held.
    void restore_text3d_models_locked() noexcept;
    [[nodiscard]] bool supports_text_animation(const Layer& layer) const noexcept;
    /// Camada de forma 3D (receita aurea-shape3d:). As partes já são nós
    /// próprios: não há preparo como o ensure_text3d_layout. modelMutex_ held.
    [[nodiscard]] bool is_shape3d_layer(const Layer& layer) const noexcept;
    bool fill_layer_detail_locked(u64 layerId, bridge::LayerDetailPOD& out) noexcept;
public:
    u32 query_curve(u64 layerId, u32 property, i32 startFrame, i32 endFrame,
                    f32* outValues, u32 sampleCount) noexcept;
    u32 query_track_curve(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex,
                          i32 startFrame, i32 endFrame, f32* outValues, u32 sampleCount) noexcept;
    bool query_keyframe_easing(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex,
                               i32 frame, f32* out4, u8* outPower = nullptr) noexcept;
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

    /// Curves: canal 0 = RGB, 1..3 = R/G/B. Pontos xy ou amostras da spline.
    u32 query_effect_curve(u64 layerId, u32 effectId, u32 param, u32 channel,
                           bool samples, f32* out, u32 capacity) noexcept;
    /// 0 mover, 1 inserir, 2 excluir, 3 identidade. Retorna índice ou -1.
    /// Ordem, limites e extremos são regras compartilhadas pelo Android/iOS.
    i32 edit_effect_curve(u64 layerId, u32 effectId, u32 param, u32 channel,
                          u32 action, u32 point, f32 x, f32 y) noexcept;

    /// Declaração dos parâmetros de um TIPO de efeito, sem precisar de layer:
    /// a ficha do catálogo (nome, tipo, faixa, unidade). `value` sai com o
    /// padrão da declaração e `animated` sai 0 — não há instância por trás.
    u32 query_effect_specs(u32 typeId, bridge::EffectParamRow* out, u32 capacity,
                           char* blob, u32 blobCapacity) noexcept;

    /// Abre/recolhe a SEÇÃO `section` (ordinal 1..N, ParamSection) do efeito no
    /// painel. Estado de apresentação da instância: gravado no projeto (byte
    /// `expanded`), sem passo de desfazer. Devolve false se a seção não existe.
    bool set_effect_section_open(u64 layerId, u32 effectId, u32 section, bool open) noexcept;

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
    /// Explicitly restores the checked export document and restarts the normal
    /// exporter at zero, rebuilding decoder/audio/temporal state. No partial
    /// video is reused. The caller supplies a fresh output destination.
    [[nodiscard]] Status restart_export(const char* recoveryPath, const char* outputPath) noexcept;
    [[nodiscard]] Status cancel_export() noexcept;
    /// 0.3.2 (CheckpointManager): PAUSA — interrompe o export (ou a partida dele)
    /// como a suspensão do app faz: o checkpoint fica para o mesmo start_export
    /// continuar. O iOS chama ao ir para segundo plano ainda no "Preparando"
    /// (a GPU não pode ser usada lá). O cancelamento do usuário é cancel_export.
    [[nodiscard]] Status pause_export() noexcept;

    /// Export retomável: um checkpoint do projeto aberto com estas configurações
    /// PEDIDAS (o degrau `safeMode` não conta) que vale a pena continuar — o app
    /// pergunta depois de o processo morrer no meio de um export, e continua no
    /// degrau que o gravou em vez de começar num mais leve. `valid` falso =
    /// nada a continuar (ou já sem progresso entre continuações).
    struct ExportResumeCandidate {
        bool valid = false;
        u32 resumeFrame = 0;
        u32 totalFrames = 0;
        u32 safeMode = 0;
    };
    [[nodiscard]] ExportResumeCandidate export_resume_candidate(const ExportSettings& requested) noexcept;

    struct ExportProgress {
        bool  running = false;
        bool  finished = false;
        Errc  result = Errc::Ok;
        u32   framesTotal = 0;
        u32   framesDone = 0;
        f32   fps = 0.0f;
        u32   etaSeconds = 0;
        char  message[128]{};
        /// Diagnóstico da sessão: média em ms por quadro de cada estágio, medida
        /// na thread que o executa. Com o pipeline sobreposto os estágios correm
        /// juntos — a soma passa do tempo de parede por quadro, e é o esperado.
        f32   decodeWaitMs = 0.0f;   ///< esperando o quadro exato do decoder
        f32   renderMs = 0.0f;       ///< CPU: preparar + gravar + submeter
        f32   readbackMs = 0.0f;     ///< esperando a GPU entregar os planos NV12
        f32   encodeMs = 0.0f;       ///< dentro do sink (write_video)
        f32   audioMs = 0.0f;        ///< mixar + write_audio
        u32   flags = 0;             ///< ExportFlag
        u32   pipelineDepth = 0;     ///< quadros em voo (1 = serial)
        /// Motivo da falha (aurea::ExportFailure, export/ExportRules.hpp): o
        /// código que as telas traduzem; `message` fica para o log. Na bridge
        /// vai nos bits 24..31 de `flags` (kExportFailureShift).
        u32   failure = 0;
    };
    /// Bits de `ExportProgress::flags` (o mesmo valor vai para a UI).
    enum ExportFlag : u32 {
        kExportHardwareEncoder = 1u << 0,   ///< o sink confirmou encoder de hardware
        kExportSoftwareEncoder = 1u << 1,   ///< caiu para encoder de software (mais lento)
        kExportThermalReduced  = 1u << 2,   ///< calor: menos quadros em voo (qualidade igual)
        kExportFrameFallback   = 1u << 3,   ///< algum quadro de vídeo saiu com o decodificado mais próximo
        kExportSafeMode        = 1u << 4,   ///< export no modo de segurança (refeito depois de o encoder travar)
        /// 0.3.2 (CheckpointManager): esta sessão continuou um export interrompido
        /// do último GOP íntegro; `framesDone` começa no quadro de onde continuou.
        kExportResumed         = 1u << 5,
        /// Terminou sem arquivo, mas o MESMO start_export (mesmas configurações e
        /// degrau) continua de um checkpoint — ou, se o encoder recusou o
        /// checkpoint, refaz esta tentativa do zero uma vez — em vez de descer a
        /// escada. Nunca depois de o usuário cancelar.
        kExportResumable       = 1u << 6,
        // Bits 7 e 12: reservados (o Android ainda lê flags antigas neles).
        // Bits 8..11: fase (kExportPhaseShift, export/OfflineFrameQueue.hpp).
        // 0.3.2: reduções que antes só iam para o log (docs/diagnostico/export-engine-2.0.md, 1.3).
        kExportBlurReduced     = 1u << 13,  ///< algum quadro saiu com menos amostras de motion blur que o pedido
        kExportResolutionCapped = 1u << 14, ///< o aparelho não comporta a resolução pedida: saiu no teto dele
        kExportEnvironmentFallback = 1u << 15, ///< a imagem HDR do ambiente 3D não coube: ambiente neutro
        // Bits 16..17: modo de segurança sugerido (kExportRetryShift).
        kExportModelFallback   = 1u << 18,  ///< modelo 3D "Original" não coube: saiu o modelo da prévia
        // Bits 16..17: o próximo modo de segurança sugerido quando o export
        // falhou pelo encoder (kExportRetryShift, export/ExportWatchdog.hpp).
    };
    [[nodiscard]] ExportProgress export_progress() const noexcept;
    /// Path-free session evidence, also readable while a codec/provider call stalls.
    [[nodiscard]] std::string export_diagnostics() const noexcept;

    /// Export como IMAGEM (export/ImageEncode.hpp): o quadro do playhead em PNG,
    /// a sequência de PNGs do trecho num .zip ou o GIF animado. O MESMO
    /// renderer e os mesmos quadros exatos do vídeo; progresso e cancelamento
    /// são os do vídeo (`export_progress`, `cancel_export`). Não precisa de
    /// encoder da plataforma.
    [[nodiscard]] Status start_image_export(const ImageExportSettings& settings, const char* outputPath) noexcept;
    /// O plano do export como imagem (dimensões, fps, quadros, alfa) com a
    /// regra do motor — o que a tela Exportar mostra é o que sai.
    [[nodiscard]] ImageExportPlan query_image_export_plan(const ImageExportSettings& settings) noexcept;

    /// Whole current composition at540p in either timeline direction. The
    /// private cache preserves alpha and expires on any content revision.
    [[nodiscard]] Status start_prerender(i32 direction) noexcept;
    void cancel_prerender() noexcept;
    [[nodiscard]] PreRenderProgress query_prerender_progress() noexcept;

    // =========================================================================
    // AureaHyper (0.3.2) — aurea/hyper/Hyper.hpp e aurea/hyper/HyperStage.hpp.
    //
    // Unidades: tempo em segundos (f64), tamanhos em pixels, fps em quadros
    // por segundo. Imagens RGBA8 sRGB com alfa reto, linha a linha, sem folga
    // (stride = 4·largura). Nenhuma destas funções pode ser chamada da thread
    // de render do motor; todas aceitam qualquer outra thread (UI incluída nas
    // que dizem "não bloqueia").
    // =========================================================================

    /// O MEDIDOR do aparelho. Sem medição guardada (ou com `remeasure`, ou com
    /// o build do motor / a GPU / o driver diferentes do que está no arquivo)
    /// MEDE: um quadro de FSR 1 480p→1080p na GPU, um tile da rede Lite no
    /// Vulkan (fp16) e na CPU com a conferência do tile contra a referência
    /// (driver que erra cai para o próximo caminho), RAM livre, ABI, GLES3,
    /// NPU (`EngineConfig::hyperNpuAvailable`) e o térmico depois do teste.
    /// O resultado vai para `cacheDirectory/hyper/device-profile.json` e fica
    /// em memória. BLOQUEIA: ~1–10 s quando mede; microssegundos depois.
    /// Thread-safe (serializada por dentro). Durante um export ou um job Hyper
    /// não mede: devolve o guardado, ou um perfil conservador com
    /// `measured == false` (Turbo) se nunca mediu. Nunca falha.
    [[nodiscard]] hyper::DeviceProfile hyper_device_profile(bool remeasure = false) noexcept;

    /// Estimativa do vídeo `sourcePath` (caminho ou "fd:<n>") com `recipe`:
    /// segundos por caminho (índice = hyper::Path; Auto = o escolhido; < 0 =
    /// indisponível neste aparelho), tamanho e fps de saída JÁ limitados pelo
    /// encoder do aparelho (a mesma regra do export) e a memória máxima do
    /// caminho escolhido. Usa o perfil guardado (não mede; sem perfil, conta
    /// com o conservador). Bloqueia só o tempo de abrir o arquivo (sonda do
    /// container, alguns ms). Fonte ilegível = Estimate com seconds todos < 0
    /// e outputWidth 0.
    [[nodiscard]] hyper::Estimate hyper_estimate(const hyper::Recipe& recipe, const char* sourcePath) noexcept;

    /// UM quadro para o deslizador antes/depois: o quadro do vídeo no instante
    /// `seconds` da FONTE (antes = a fonte ampliada por bilinear para o tamanho
    /// de saída; depois = a receita no caminho `path`; Auto = o do perfil).
    /// Motion não muda um quadro parado (o "depois" mostra as outras etapas).
    /// Saída: RGBA8 sRGB `width`×`height` (o tamanho de saída limitado a 1920
    /// no lado maior para caber na tela). BLOQUEIA: decodifica e renderiza
    /// (Turbo ~50–300 ms; Lite/Max até alguns segundos). Recusa (Busy) durante
    /// um export ou job Hyper. Cancelar: não há; é um quadro.
    [[nodiscard]] Status hyper_preview_frame(const hyper::Recipe& recipe, const char* sourcePath, f64 seconds,
                                             hyper::Path path, std::vector<u8>& beforeRgba,
                                             std::vector<u8>& afterRgba, u32& width, u32& height) noexcept;

    /// O JOB DA HOME: um projeto temporário (fora do projeto aberto, que não
    /// muda) com o vídeo `sourcePath` e os efeitos Hyper da receita, passado
    /// pelo export do motor — herda checkpoint/retomada, export em segundo
    /// plano, validação, publicação atômica e progresso honesto. Com etapas
    /// Lite/Max, o grafo Hyper (HyperStage.hpp) roda entre o readback e o
    /// encoder; se uma etapa Turbo vier depois de uma rede (o Motion, por
    /// exemplo, interpola DEPOIS do upscale), o job faz uma segunda passagem
    /// pelo export a partir de um intermediário (apagado no fim).
    ///
    /// Tempo exato: 60 fps (targetFps) mantém a duração e o som; câmera lenta
    /// ×N multiplica a duração por N exatamente e sai SEM som (não esticamos o
    /// áudio; `JobSteps::audioMuted`). Saída limitada pelo encoder do aparelho.
    ///
    /// Não bloqueia: valida, sonda a fonte e devolve; o trabalho corre numa
    /// thread própria. Progresso: `export_progress()` (quadros, ETA, fase; não
    /// cai para "terminado" entre passagens) + `hyper_job_steps()`. Cancelar:
    /// `cancel_export()` (para na hora). Chamar de novo com a MESMA fonte,
    /// receita e saída depois de o app morrer continua do que já estava pronto
    /// (passagens validadas e o checkpoint do export). InvalidState se já há
    /// um export/job rodando.
    [[nodiscard]] Status start_hyper_job(const char* sourcePath, const hyper::Recipe& recipe,
                                         const char* outputPath) noexcept;

    /// Os passos do job (quais existem, quais terminaram, o atual). Não
    /// bloqueia (cópia sob uma trava curta). Sem job: `active == false`.
    [[nodiscard]] hyper::JobSteps hyper_job_steps() const noexcept;

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

    /// Quanto cada etapa de `initialize` custou nesta execução (§59: abertura
    /// medida, não estimada). Sai uma linha no log e o teste de abertura lê.
    struct StartupTimings {
        f32 jobsMs = 0.0f;       ///< detecção do aparelho + workers
        f32 gpuMs = 0.0f;        ///< backend: instância, dispositivo, cache de pipeline
        f32 rendererMs = 0.0f;   ///< shaders + pipelines pré-aquecidos
        f32 restMs = 0.0f;       ///< fontes, mídia, áudio, miniaturas
        f32 totalMs = 0.0f;
        u32 pipelinesPrewarmed = 0;
    };
    [[nodiscard]] const StartupTimings& startup_timings() const noexcept { return startup_; }

    [[nodiscard]] const std::string& cache_directory() const noexcept { return config_.cacheDirectory; }
    [[nodiscard]] const std::string& documents_directory() const noexcept { return config_.documentsDirectory; }

private:
    [[nodiscard]] Status apply_command_internal(const Command& cmd, const char* stringData,
                                                bool recordUndo) noexcept;
    /// Divide o orçamento de memória pelas categorias. Chamado na subida e de
    /// novo quando a GPU real chega e muda o orçamento.
    void apply_memory_budgets() noexcept;
    /// Orçamento do processo: o configurado, senão o medido, preso pelo teto
    /// do aparelho (`memoryBudgetCapBytes`) e pelo espaço de endereços.
    [[nodiscard]] u64 process_budget_bytes() const noexcept;
    /// Maior lado de textura aceito ao importar modelo 3D — e o MESMO ao
    /// reabrir, senão o quadro muda entre importar e reabrir o projeto.
    [[nodiscard]] u32 model_texture_cap() const noexcept;
    /// A memória da plataforma completada com a medida no início (DeviceCapabilities).
    [[nodiscard]] scene3d::DeviceMemoryHint model_memory_hint(const scene3d::DeviceMemoryHint& fromPlatform) const noexcept;
    /// Opções de import de um modelo com o orçamento do aparelho. `quality` é
    /// a pedida; com `escalate`, sobe para a mais leve que couber (reabrir um
    /// projeto feito num aparelho maior). Devolve a usada em `used`.
    /// `triangleCeiling` (os triângulos guardados no asset): reabrir nunca sai
    /// mais pesado do que o import otimizado saiu, mesmo num aparelho maior.
    [[nodiscard]] scene3d::ImportOptions model_import_options(const std::string& path, scene3d::ModelQuality quality,
                                                              const scene3d::DeviceMemoryHint& memory, bool escalate,
                                                              scene3d::ModelQuality* used = nullptr,
                                                              u32 triangleCeiling = 0) const noexcept;
    /// Snapshot "antes" no histórico, se o comando altera a composição.
    void record_history_locked(CommandType type) noexcept;
    [[nodiscard]] static bool mutates_model(CommandType type) noexcept;
    /// Depois de desfazer/refazer: seleção sem layers mortas, fontes de mídia
    /// órfãs fechadas, playback no tamanho novo.
    void after_history_restore_locked() noexcept;
    void drain_commands_locked() noexcept;
    [[nodiscard]] Composition* current_composition() noexcept;
    [[nodiscard]] Status recover_device_locked() noexcept;
    [[nodiscard]] Status ensure_gpu_healthy_locked() noexcept;
    // renderMutex_: a failed export may leave a real submission in flight.
    [[nodiscard]] Status poll_export_gpu_locked(u64 timeoutNs = 0) noexcept;
    void drain_capture_target_locked() noexcept;
    void retain_failed_export_gpu_locked() noexcept;
    void render_thread_main() noexcept;
    /// Põe na fila do proxy os vídeos usados pelo projeto (import, abertura,
    /// troca de modo/escala). Só consulta o disco ou gera em segundo plano.
    void queue_project_proxies_locked() noexcept;
    void update_perf(const FrameStats& stats, const RenderTimings& timings,
                     const FrameSnapshot& snap, u64 frameStartNs) noexcept;
    [[nodiscard]] RenderSettings current_render_settings() noexcept;
    struct OffscreenRequest;
    [[nodiscard]] Status render_offscreen_impl(TextureHandle target, u32 width, u32 height, bool asPreview,
                                               u64 expectedGpuGeneration, bool targetPreviewScale,
                                               OffscreenRequest& request) noexcept;
    [[nodiscard]] Status capture_frame_rgba_impl(u32 maxDim, std::vector<u8>& out, u32& width, u32& height,
                                                bool asPreview) noexcept;
    static const ImagePixels* image_lookup(void* self, AssetId id);
    static void on_frame_ready(void* self);

    [[nodiscard]] AdaptiveResolutionController& adapt() noexcept { return *adaptive_; }

    EngineConfig       config_{};
    StartupTimings     startup_{};
    std::atomic<EngineState> state_{EngineState::Uninitialized};
    Errc               lastError_ = Errc::Ok;
    char               lastErrorDetail_[128]{};
    /// Guarda o último erro (código padronizado + texto sem dado do usuário) e
    /// loga com o nome §115.
    void set_last_error(Status s, const char* context) noexcept;

    // --- Gravação / abertura (Fase 8G) ---------------------------------------
    std::mutex         saveMutex_;              ///< uma gravação por vez
    u64                projectSession_ = 0;     ///< under modelMutex_; changes on replacement
    struct ExportRestartGuard {
        u64 session = 0, generation = 0;
        u32 revision = 0;
        CompositionId composition{};
    };
    [[nodiscard]] bool export_restart_matches_locked(const ExportRestartGuard& guard) const noexcept;
    [[nodiscard]] Status load_project_checked(const char* path, ExportRestartGuard* guard) noexcept;
    /// `hyper` (job Hyper, src/hyper/HyperExport.hpp): exporta o projeto que
    /// vem nele em vez do aberto, com o grafo Hyper entre readback e encoder.
    [[nodiscard]] Status start_export_checked(const ExportSettings& settings, const char* outputPath,
                                              const ExportRestartGuard* guard,
                                              const HyperExportOverride* hyper = nullptr) noexcept;
    [[nodiscard]] Status save_project_impl(const char* path, bool idleOnly = false, bool dirtyOnly = false,
                                           bool* saved = nullptr) noexcept;
    mutable std::mutex saveStatsMutex_;
    SaveStats          saveStats_{};
    /// O arquivo principal do projeto aberto estava ruim (abriu da cópia ou
    /// parcial): a próxima gravação NÃO o gira para `.bak` — senão o lixo
    /// tomaria o lugar do último estado válido. Sob modelMutex_.
    bool               mainFileSuspect_ = false;
    std::atomic<u32>   lastLoadNotice_{0};
    std::atomic<u32>   lastLoadMissing_{0};

    DeviceCapabilities caps_{};
    JobSystem          jobs_{};
    MemoryManager      memory_{};
    History            history_{};
    tracking::RealCameraCapture realCameraPose_;
    RealCameraStatus realCameraState_;
    CompositionId realCameraComp_{};
    LayerId realCameraLayer_{};
    bool realCameraPlaybackStarted_ = false;
    void real_camera_end_locked(bool pause) noexcept;
    [[nodiscard]] Status real_camera_commit_locked() noexcept;
    [[nodiscard]] Layer* real_camera_preview_layer_locked() noexcept;
    EffectRegistry     effectRegistry_{};
    /// Depois do registro: morre antes dele (tira os efeitos que pôs lá).
    std::unique_ptr<plugins::PluginManager> plugins_;
    /// O quadro de teste da instalação: `typeId` sozinho em 256×256, já com os
    /// pipelines prontos. Toma o renderMutex_ (espera o preview soltar a GPU).
    [[nodiscard]] bool plugin_test_render(const EffectRegistry& registry, EffectTypeId typeId, f32& ms,
                                          std::string& error) noexcept;
    // 0.3.4 plugins: runtimes de script (EnginePluginsV2.cpp). shared_ptr: o
    // tipo só é completo lá (o destrutor do Engine não precisa conhecê-lo).
    struct PluginScriptState;
    std::shared_ptr<PluginScriptState> pluginScripts_;
    [[nodiscard]] std::shared_ptr<plugins::PluginRuntime> plugin_runtime_for(const char* id, std::string& error) noexcept;
    [[nodiscard]] std::string plugin_run(const char* id, const std::function<std::string(plugins::PluginRuntime&,
                                         const plugins::PluginRuntime::ApiInstaller&, const plugins::CallHooks&)>& body) noexcept;
    void install_plugin_api(plugins::script::Context& ctx, plugins::PluginRuntime& runtime);
    AdaptiveResolutionController* adaptive_ = nullptr;

    std::unique_ptr<GPUBackend> gpu_;
    u64                pendingExportGpuFrame_ = 0, pendingExportGpuGeneration_ = 0; ///< renderMutex_
    std::atomic<u64>   exportOriginalModelBytes_{0}; ///< extra CPU sources, outside the preview/shared model map
    u64                gpuGeneration_ = 0; ///< under renderMutex_; survives backend/device recreation
    bool               gpuRecoveryPending_ = false; ///< renderMutex_; failed rebuild remains retryable
    Renderer           renderer_;
    /// Embrulha a fábrica da plataforma (sequência de imagens no motor). Antes
    /// de media_/thumbs_/audio_: eles usam a fábrica até serem destruídos.
    ImageSequenceFactory sequenceFactory_;
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
    /// Sobe `modelRevision_` numa edição que não muda o SOM (marcas). O
    /// snapshot de áudio continua valendo: reconstruí-lo tocando descartava o
    /// anel e re-ancorava o relógio mestre (tranco no som e no vídeo). Sob o
    /// lock do modelo.
    void bump_revision_without_audio_locked() noexcept;
    static std::string audio_path_resolver(void* self, const std::string& stored);
    FrameScheduler     frameScheduler_;
    FrameSnapshot      snapshot_;

    std::unique_ptr<CommandQueue> commandQueue_;
    std::unique_ptr<Project>      project_;
    std::vector<CompositionId>    compStack_;   ///< caminho da principal até a aberta
    // Omega chrome (área 02): modos de visão da prévia (EngineOmegaChrome.cpp; sob modelMutex_).
    u32 omegaViewMode_ = 0;              ///< 0 normal, 1 só as escolhidas, 2 escolhidas a 50 %
    bool omegaPixelPreview_ = false;
    std::vector<u64> omegaViewIds_;      ///< ids empacotados, ordenados
    void apply_omega_view_locked(RenderSettings& rs) const noexcept;
    std::unordered_map<u64, std::shared_ptr<const scene3d::HdriPixels>> hdris_;
    std::unordered_map<u64, std::shared_ptr<const CubeLut>> cubeLuts_;
    struct CameraTrackJob;
    std::unique_ptr<CameraTrackJob> cameraTrack_;
    void join_camera_track() noexcept;
    struct MotionTrackJob;
    std::shared_ptr<MotionTrackJob> motionTrack_;
    void join_motion_track() noexcept;
    // 0.3.6 AutoRig 3D (EngineAutoRig.cpp): um trabalho por vez, thread própria.
    struct AutoRigJob;
    std::shared_ptr<AutoRigJob> autoRig_;
    void join_autorig() noexcept;
    bool start_autorig_job(u64 layerId, const rig::AutoRigJoints* edit) noexcept;
    // 0.3.6 Motion Capture (EngineMocap.cpp): a sessão tem trava e atômicos próprios.
    struct MocapSession;
    std::mutex mocapMutex_;                       ///< só o ponteiro (nunca segurado no solver)
    std::shared_ptr<MocapSession> mocap_;
    [[nodiscard]] std::shared_ptr<MocapSession> mocap_session() noexcept;
    void set_mocap_preview(u64 layerId, std::shared_ptr<const rig::MocapPreview> preview) noexcept;
    static std::shared_ptr<const scene3d::HdriPixels> hdri_lookup(void* self, AssetId id);
    static std::shared_ptr<const CubeLut> cube_lookup(void* self, AssetId id);
    struct ClipboardMedia {
        u64 session = 0;
        std::unordered_map<u64, Asset> assets;
        std::unordered_map<u64, std::shared_ptr<const Composition>> compositions;
        std::unordered_map<u64, std::shared_ptr<const ImagePixels>> images;
        std::unordered_map<u64, std::shared_ptr<const scene3d::SceneAsset>> models;
        std::unordered_map<u64, std::shared_ptr<const scene3d::HdriPixels>> hdris;
        std::unordered_map<u64, std::shared_ptr<const CubeLut>> luts;
    };
    void capture_clipboard_media_locked(ClipboardMedia& bundle, const std::vector<Layer>& layers);
    void restore_clipboard_media_locked(const ClipboardMedia& bundle, std::vector<Layer>& layers);
    struct Clipboard {
        ClipboardMedia layerMedia, effectMedia;
        f64 layersFps = 0;
        std::vector<std::pair<u64, Layer>> layers;   ///< id original → cópia
        i64 layersAnchor = 0;
        bool hasStyle = false;
        Layer style;
        bool hasTransform = false;
        Layer transform;
        std::vector<EffectInstance> effects;
        std::vector<Track> effectTracks;            ///< EffectParam dos efeitos copiados
        i64 effectsBase = 0;                        ///< offset da camada de origem (início dela em tempo local)
        f64 effectsFps = 0.0;                       ///< fps da composição de origem (colar numa pré-comp com outro fps reescala)
        struct Key { TrackProperty property; u32 effectIndex; u32 effectParamIndex; EffectTypeId effectType; Keyframe key; u32 effectOrdinal = 0; };
        struct MeshKey { u32 effectOrdinal; u32 rows; u32 cols; MeshWarpKey key; };
        std::vector<MeshKey> meshKeys;
        std::vector<Key> keys;                       ///< tempo relativo ao instante copiado (0)
        /// Copiar animação: os keyframes de FORMA (morph) da camada vetorial,
        /// por (grupo, caminho), com o tempo relativo como `keys`.
        struct PathKeys { u32 group; u32 path; std::vector<PathKey> keys; };
        std::vector<PathKeys> pathKeys;
        std::vector<LayerAnimator> layerAnimators;   ///< animadores de camada copiados
        std::vector<Track> layerAnimTracks;          ///< trilhas deles (effectIndex = posição na cópia)
    } clipboard_;
    /// Por AssetId empacotado. Pixels IMUTÁVEIS e compartilhados: trocar uma
    /// imagem cria outro objeto. O export (snapshot offline), o colar e a
    /// recuperação seguram ponteiros, não cópias — copiar todas as fotos do
    /// projeto na largada do export estourava o orçamento ("erro 9") em
    /// projetos com muitas imagens.
    std::unordered_map<u64, std::shared_ptr<const ImagePixels>> images_;
    /// Modelos 3D carregados, por AssetId. Um por asset, qualquer número de
    /// layers (ModelInstance) apontando para ele.
    std::unordered_map<u64, std::shared_ptr<const scene3d::SceneAsset>> models_;
    static std::shared_ptr<const scene3d::SceneAsset> model_lookup(void* self, AssetId id);
    [[nodiscard]] std::string resolve_asset_path(const std::string& stored) const;
    [[nodiscard]] u32 caption_count_locked(const Composition& comp, u64 sourceLayer) const noexcept;
    void migrate_echo_to_effect() noexcept;
    [[nodiscard]] std::string store_asset_path(const std::string& absolute) const;
    /// Arquivos de entrada da sequência: pastas listadas, um quadro vira os
    /// irmãos da pasta, um .zip é extraído (EngineImageSequence.cpp).
    [[nodiscard]] Status collect_sequence_inputs(const std::vector<std::string>& paths, std::vector<std::string>& files,
                                                 std::string& preferred, bool& fromArchive) noexcept;
    [[nodiscard]] Result<ImageSequenceProbe> probe_sequence_files(const std::vector<std::string>& paths,
                                                                  ImageSequenceSpec& spec) noexcept;

    LayerId rawPlaybackLayer_{};
    SceneEditorView sceneEditor_{}; ///< modelMutex protected, never serialized
    std::vector<u64> selection_;
    std::atomic<u32> modelRevision_{1};   ///< a UI relê listas quando muda
    /// Subidas de `modelRevision_` que mudam só um TRECHO da composição atual
    /// (cortar/aparar/mover uma camada sem ninguém olhando para ela em outro
    /// instante). A chave da prévia guardada é `modelRevision_ - isto`: essas
    /// edições não descartam o cache inteiro; o trecho vai em
    /// `pendingPreviewRanges_` (modelMutex) e sai no próximo quadro.
    std::atomic<u32> previewRangeEdits_{0};
    std::vector<std::pair<i64, i64>> pendingPreviewRanges_;
    [[nodiscard]] u64 preview_cache_revision() const noexcept {
        return static_cast<u64>(modelRevision_.load(std::memory_order_acquire)
                                - previewRangeEdits_.load(std::memory_order_acquire)) + 1;
    }

    // --- Sincronização --------------------------------------------------------
    /// Protege projeto, timeline, playback, seleção e imagens.
    mutable std::mutex modelMutex_;
    ModelImportReport lastModelImport_{};   ///< sob modelMutex_
    std::mutex commandSubmitMutex_; ///< publication vs session replacement; never waits for rendering
    /// Protege backend e renderer (GPU). Nunca adquirido com o do modelo já
    /// preso por outra thread que espere o de render — a ordem é sempre
    /// modelo → render, ou render sozinho.
    std::mutex renderMutex_;
    std::atomic<u32> localAiStatusSnapshot_{0};

    std::thread renderThread_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    bool wakeFlag_ = false;
    // Ritmo do preview pelo conteúdo (render_frame(onlyIfChanged)).
    std::atomic<bool> forceRender_{true};     ///< a UI mudou algo / superfície nova
    std::atomic<u64> previewRetryDueNs_{0};   ///< retry de recurso/GPU; uma edição cancela a espera
    std::atomic<u64>  mediaReadyGen_{0};      ///< frames novos do decoder
    i64  lastRenderedFrame_ = -1;
    PreviewRefill previewRefill_;
    PreviewIdleBuffer previewIdle_;          ///< render thread / renderMutex_
    std::atomic<bool> livePreviewGraphActive_{false}; ///< secondary effect-preview early Busy without renderMutex_
    PreviewIdleKey livePreviewGraphKey_{};
    u64 livePreviewGraphPlaybackGeneration_ = 0, livePreviewGraphGpuGeneration_ = 0;
    bool livePreviewGraphPlaying_ = false, livePreviewGraphRefine_ = false;
    void cancel_preview_graph_locked() noexcept;
    [[nodiscard]] bool continue_preview_graph_locked(u64 frameStart, Status& result) noexcept;
    std::atomic<u64> previewPressureUntilNs_{0}; ///< trim_memory(>= RUNNING_LOW): prévia guardada pausada até aqui
    std::mutex offscreenRequestMutex_;       ///< one synchronous capture owns prepare through readback
    std::atomic<u32> offscreenReaders_{0};    ///< display/idle work must not steal capture graph or decode targets
    std::atomic<bool> offscreenCancelled_{false}; ///< lifecycle interrupts capture segments before joining ownership
    std::mutex captureRetirementMutex_;      ///< timeout queues cleanup without waiting for render ownership
    TextureHandle pendingCaptureTarget_{};
    GPUBackend* pendingCaptureGpu_ = nullptr;
    u64 pendingCaptureGeneration_ = 0;
    // Protected by modelMutex_. The published packed status is lock-free for UI.
    bool previewBuffering_ = false;
    i64 previewBufferStart_ = 0;
    u64 previewBufferSince_ = 0;
    u32 previewBufferReady_ = 0, previewBufferTarget_ = 0;
    /// Quadros perdidos até o último Play e se a reprodução anterior engasgou
    /// (a pré-render do próximo Play prepara mais quadros).
    u32 previewDroppedAtPlay_ = 0;
    bool previewStuttered_ = false;
    std::atomic<u32> previewBufferStatus_{0};
    u32  lastRenderedRevision_ = 0;           ///< modelRevision_ do último frame desenhado
    u32  incompleteRetries_ = 0;              ///< quadros seguidos com camada pendente
    u64  lastMediaGen_ = 0;
    bool lastIncomplete_ = true;              ///< último frame tinha vídeo faltando/aproximado
    bool lastSkipped_ = false;
    // Refino do preview parado: o AUTO baixa a resolução para manter o ritmo;
    // parado, um quadro na melhor resolução permitida substitui o reduzido.
    bool refinePending_ = false;              ///< último quadro saiu reduzido
    bool refineNow_ = false;                  ///< este quadro é o refino
    u64 refineDueNs_ = 0;                     ///< prazo absoluto; vsync sem mudança não o adia
    /// 0.4.0 "Toque leve" (interaction_draft): grupos de desfazer abertos =
    /// dedo arrastando; o quadro desenhado em rascunho não entra na média do AUTO.
    std::atomic<u32> interactionDepth_{0};
    std::atomic<bool> lastFrameDrafted_{false};
    bool contentBoundedPlayback_ = false;
    // A paused transport jump may target a project marker after the last clip.
    // Keep that cursor across preview reconfiguration while the marker exists.
    i64 pausedTimelineEventFrame_ = -1;
    u64 pausedTimelineEventSession_ = 0;
    CompositionId pausedTimelineEventComposition_{};
    void configure_playback_locked(const Composition* comp, i64 rawDuration = -1) noexcept;
    i64 new_layer_end_locked(const Composition* comp, const Layer* self, i64 start) const noexcept;
    /// Mídia importada que o modo Edição pôs numa LINHA MAGNÉTICA entra encostada
    /// no fim dela (nunca no cabeçote, que sobreporia os cortes); `length` em
    /// quadros. Devolve se reposicionou. Chamar com `modelMutex_` travado.
    bool place_on_magnetic_line_locked(Composition& comp, LayerId id, i64 length) noexcept;
    /// Interface de Edição: o áudio vai para uma LINHA DE ÁUDIO (só áudio, sem
    /// ímã) no tempo que já tem, embaixo da trilha principal — a primeira linha de
    /// áudio livre nesse trecho, ou uma nova. Nunca entra na sequência dos vídeos.
    /// Chamar com `modelMutex_` travado.
    void place_on_audio_line_locked(Composition& comp, LayerId id) noexcept;
    [[nodiscard]] i64 navigation_end_locked(const Composition* comp) const noexcept;
    u64  nextFrameDueNs_ = 0;                 ///< quando o playhead muda de frame (tocando)
    std::atomic<bool> renderRunning_{false};
    std::atomic<bool> playingHint_{false};
    std::atomic<bool> surfaceAttached_{false};
    std::atomic<u32>  lastCulledLayers_{0};       ///< telemetria (Fase 8C)
    std::atomic<u64>  renderWakeups_{0};

    SurfaceDesc surface_{};
    std::atomic<u64> pendingSurfaceSize_{0}; ///< UI publishes; render applies between frames
    /// attach/detach adiados porque o export segurava renderMutex_ (ver attach_surface).
    struct PendingSurfaceAttach { void* window = nullptr; u32 width = 0, height = 0; bool valid = false; };
    std::mutex surfacePendingMutex_;
    PendingSurfaceAttach pendingAttach_{};           ///< surfacePendingMutex_
    std::atomic<bool> surfaceDetachPending_{false};
    std::atomic<bool> surfaceOpPending_{false};
    void apply_pending_surface_locked(bool allowAttach = true) noexcept;    ///< renderMutex_

    // --- Métricas ---------------------------------------------------------------
    mutable std::mutex perfMutex_;
    bridge::PerfPOD perf_{};
    struct PlaybackMeasurement {
        u64 lastNs = 0, activeNs = 0, presented = 0, repeated = 0, dropped = 0, epoch = 0;
        u64 rateFrames = 0, lastDecoded = 0, decoded = 0;
        i64 lastPts = -1, lastDuration = 0;
        bool playing = false;
        std::string report;
    } playbackMeasurement_;

    FrameStats lastFrame_{};
    u64 frameCounter_ = 0;
    u64 fpsWindowStartNs_ = 0;
    u32 fpsWindowFrames_ = 0;
    f32 measuredFps_ = 0.0f;
    // Ritmo dos quadros (§145): intervalo entre quadros APRESENTADOS tocando,
    // num anel fixo (sem alocação); percentis recalculados uma vez por janela.
    static constexpr u32 kPacingRing = 128;
    f32 pacingMs_[kPacingRing]{};
    u32 pacingCount_ = 0;
    u32 pacingHead_ = 0;
    u64 lastPresentNs_ = 0;
    f32 pacingP50_ = 0.0f, pacingP95_ = 0.0f, pacingP99_ = 0.0f, pacingStd_ = 0.0f;
    u32 pacingSamples_ = 0;
    void roll_pacing() noexcept;
    // Benchmark offscreen (Fase 8A).
    bool offscreenTimers_ = false;
    OffscreenMeasure offscreenMeasure_{};
    GpuTiming offscreenPasses_[64]{};

    struct ExportContext;
    // Lifecycle operations may arrive from different native/UI worker queues.
    // Workers never acquire this mutex, so joining them while holding it is safe.
    mutable std::mutex exportLifecycleMutex_;
    // Short-lived pointer lock: progress/cancel never wait for encoder startup
    // or for the lifecycle join, only for publication of a new context.
    mutable std::mutex exportContextMutex_;
    mutable std::mutex exportDiagnosticsMutex_;
    std::shared_ptr<ExportDiagnostics> exportDiagnostics_;
    std::unique_ptr<ExportContext> exportCtx_;
    // Native calls may return after watchdog abandonment. Keep at most two
    // sessions until their own workers close the sink and publish their exit.
    std::array<std::unique_ptr<ExportContext>, 2> retiredExports_; ///< lifecycle + renderMutex_
    /// Startup resources held until their submitted GPU fence is terminal.
    std::unique_ptr<ExportContext> pendingExportStartup_; ///< renderMutex_
    // Bound from startup context creation through publication when gated. The pointer lock prevents a
    // public cancellation from racing its scoped removal/context destruction.
    std::atomic<bool>* exportStartupCancel_ = nullptr;
    std::atomic<bool>* preRenderStartupCancel_ = nullptr; // same short context lock
    /// O cancelamento em curso vem da suspensão do app (ou do shutdown), não
    /// do usuário: o checkpoint do export fica para continuar. Limpo no resume.
    std::atomic<bool> exportSuspendCancel_{false};
    /// "Preparando" (hash das fontes do checkpoint): bytes lidos/total, para
    /// export_progress mostrar a preparação andando antes de haver contexto.
    std::atomic<bool> exportPreparing_{false};
    std::atomic<u64> exportPrepareDone_{0}, exportPrepareTotal_{0}, exportPrepareBeatNs_{0};
    /// Export em andamento: o render do preview não toca na GPU nem nos
    /// decoders (que o export usa em sequência).
    std::atomic<bool> exportActive_{false};
    /// Aviso de memória do sistema durante o export (0.3.0): o estágio pedido
    /// (TrimStage). O renderizador do export é outro objeto e trim_memory não
    /// pode mexer nele no meio de um quadro; a thread do export o atende entre
    /// dois quadros (export_thread_main). 0 = nada pendente.
    std::atomic<u8> exportTrimRequest_{0};
    /// O decoder entregou quadro: acorda o export que espera o quadro exato
    /// (sem dormir 5 ms às cegas por tentativa).
    std::mutex exportWakeMutex_;
    std::condition_variable exportWakeCv_;
    /// Calor (set_thermal): o export reduz quadros em voo, nunca a qualidade.
    std::atomic<bool> thermalDegrade_{false};
    void export_thread_main() noexcept;
    void export_encoder_main() noexcept;
    /// Reclaims returned workers and bounds retained native sessions before
    /// another export starts. Called after producer join with renderMutex_.
    [[nodiscard]] Status retire_abandoned_export_locked(bool admitNewSession = true) noexcept;
    void image_export_thread_main() noexcept;
    [[nodiscard]] Status start_image_export_internal(const ImageExportSettings& settings, const char* outputPath,
                                                     i32 preRenderDirection) noexcept;
    /// `error` is published as the progress error (Cancelled for edits/cancel;
    /// the real cause when the producer itself failed).
    void invalidate_prerender(i32 error = static_cast<i32>(Errc::Cancelled)) noexcept;
    /// exportLifecycleMutex_: a PreRender cancelled by an edit/Cancel keeps its
    /// worker until the current frame returns. A new PreRender waits for it
    /// (bounded, cancellable) instead of failing with "export em andamento".
    [[nodiscard]] Status await_cancelled_prerender_worker(u64 deadlineNs, bool forExport = false) noexcept;
    /// Startup waits that depend on the phone (slow GPU finishing a cancelled
    /// frame, slow flash removing the previous cache). Cancel ends them at once.
    static constexpr u64 kPreRenderStartupWaitNs = 30'000'000'000ull;
    /// Cancela um PreRender em andamento para o export usar o contexto.
    /// true = havia worker de PreRender (o chamador espera ele sair).
    bool cancel_prerender_for_export() noexcept;
    static Status export_too_long_status() noexcept;
    u32 bump_model_revision_locked() noexcept;
    [[nodiscard]] bool prerender_valid_locked() noexcept; // modelMutex_
    void clear_prerender_gpu_locked() noexcept; // renderMutex_, backend owns deferred destruction
    std::unique_ptr<PreRenderCache> preRenderCache_;
    mutable std::mutex preRenderStateMutex_;
    PreRenderProgress preRenderProgress_{};
    PreRenderCache::Key preRenderKey_{};
    std::atomic<u64> preRenderToken_{0};
    std::atomic<bool> preRenderCancel_{false}, preRenderActive_{false};
    struct PreRenderGpuSlot {
        TextureHandle texture{};
        u64 generation = 0, gpuFrame = 0, token = 0;
        i64 time = -1;
    };
    std::array<PreRenderGpuSlot, 2> preRenderTextures_{};
    std::vector<u16> preRenderUpload_;
    /// renderMutex_: início da sequência de quadros que o leitor do PreRender
    /// não entregou TOCANDO (0 = entregando). Passado kPreRenderStallNs, a
    /// prévia volta ao render ao vivo em vez de congelar a imagem.
    u64 preRenderStallSinceNs_ = 0;
    static constexpr u64 kPreRenderStallNs = 250'000'000ull;
    [[nodiscard]] Status initialize_offline_export(ExportContext& ctx) noexcept;
    [[nodiscard]] Status prepare_export_resources(ExportContext& ctx) noexcept;
    void release_offline_export(ExportContext& ctx, bool retainReadbacks = false) noexcept;
    [[nodiscard]] Status render_export_frame(FrameIndex t, const OffscreenTarget& target, u64& gpuFrame) noexcept;
    [[nodiscard]] Status wait_export_gpu(u64 gpuFrame) noexcept; // renderMutex_ held by caller
    /// O contexto vem explícito: um worker abandonado (ExportWatchdog.hpp) que
    /// volte depois nunca pode tocar no export SEGUINTE por `exportCtx_`.
    [[nodiscard]] Status write_export_audio(ExportContext& ctx, i64 untilSample) noexcept;
    /// Uma linha no log com o custo do export (telemetria): CPU do 1º quadro e
    /// dos demais, GPU por quadro e os passes mais caros.
    void log_export_profile() noexcept;
    /// Âncora da camada de texto no centro da caixa atual (depois de editar).
    void recenter_text(Layer& l) noexcept;

    // Omega painel: (área 04) a camada de partida do arrasto de velocidade
    // (src/engine/EngineOmegaLayer.cpp; sob modelMutex_).
    struct OmegaLayerState;
    struct OmegaLayerStateDeleter { void operator()(OmegaLayerState* s) const noexcept; };
    std::unique_ptr<OmegaLayerState, OmegaLayerStateDeleter> omegaLayer_;
    [[nodiscard]] OmegaLayerState& omega_layer_state() noexcept;

    // --- AureaHyper (src/engine/EngineHyper.cpp) -----------------------------
    struct HyperState;
    struct HyperStateDeleter { void operator()(HyperState* s) const noexcept; };
    std::unique_ptr<HyperState, HyperStateDeleter> hyper_;
    [[nodiscard]] HyperState& hyper_state() noexcept;
    /// cancel_export: o job para na hora (a passagem atual e as próximas).
    bool hyper_cancel_job() noexcept;   ///< true = havia job
    /// shutdown: cancela e espera a thread do job.
    void hyper_shutdown() noexcept;
    /// export_progress durante um job: não "termina" entre passagens.
    void hyper_overlay_progress(ExportProgress& p) const noexcept;
    /// Um export comum começou: o progresso dele não é mais o do último job.
    void hyper_forget_job() noexcept;
    void hyper_job_main() noexcept;
    /// O perfil guardado (memória/disco), sem medir. Nunca bloqueia na GPU.
    [[nodiscard]] hyper::DeviceProfile hyper_cached_profile() noexcept;
    /// Worker do encoder com o grafo Hyper (Engine.cpp): o quadro lido vai
    /// para o grafo e o slot é solto; o que sai do grafo, em ordem, vai para o
    /// sink com o áudio até o fim dele.
    void export_encoder_hyper_loop(ExportContext& ctx) noexcept;
};

} // namespace aurea
