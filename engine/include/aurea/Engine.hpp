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
#include "aurea/project/Presets.hpp"
#include "aurea/export/ExportSink.hpp"
#include "aurea/scene3d/Importer.hpp"
#include "aurea/scene3d/Text3D.hpp"
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
    /// Quadros de export em voo (GPU renderizando um enquanto o encoder recebe
    /// o anterior). 0 = automático (3; 1 sob calor). 1 = serial — o teste de
    /// equivalência compara os dois bytes a bytes.
    u32 exportPipelineDepth = 0;

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
    /// Fonte padrão do texto (arquivo TTF/OTF); vazio = procurar a do sistema.
    std::string defaultFontPath;
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

    /// Pressão de memória do SISTEMA (Fase 8 §13): o nível do Android
    /// (ComponentCallbacks2.TRIM_MEMORY_*: 5, 10, 15, 20, 40, 60, 80) vira um
    /// estágio da ordem de despejo — miniaturas fora da tela, waveform antiga,
    /// quadros sem uso, cache de render antigo, mips altos, assets 3D sem uso,
    /// temporários. Projeto, alterações não salvas, histórico e timeline nunca
    /// entram. Qualquer thread. Devolve o relatório (bytes por estágio; os de
    /// GPU medidos no backend).
    MemoryManager::TrimReport trim_memory(i32 osLevel) noexcept;

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
    [[nodiscard]] Status new_project(u32 width = 1920, u32 height = 1080,
                                     f64 fps = 30.0, const char* title = nullptr) noexcept;
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

    /// Forma (SDF): os mesmos parâmetros de `ShapeSetParam` (1 raio, 2 lados,
    /// 3 raio interno, 4 contorno, 5 largura, 6 altura), agora ANIMÁVEIS.
    /// `out` recebe os 7 valores no playhead + bits de animado + bits de
    /// keyframe aqui (kShapeParamFloats no total).
    static constexpr u32 kShapeParamCount = 7;
    static constexpr u32 kShapeParamFloats = kShapeParamCount + 2;
    u32 query_shape_params(u64 layerId, f32* out, u32 capacity) noexcept;
    bool set_shape_param(u64 layerId, u32 param, f32 value, bool continuing) noexcept;
    bool toggle_shape_param_key(u64 layerId, u32 param) noexcept;
    /// Desenho à mão livre: pontos do dedo (x,y em px da composição) viram um
    /// caminho suave (ajuste de Schneider, `error` px). `layerId` = 0 cria uma
    /// camada vetorial nova; senão entra como grupo novo nela. Devolve a camada.
    [[nodiscard]] Result<u64> add_freehand_path(u64 layerId, const f32* xy, usize count, f32 error) noexcept;
    /// SVG → camada vetorial nova (um grupo por forma), centrada na composição.
    [[nodiscard]] Result<u64> import_svg(const std::string& text, const char* name) noexcept;
    /// Texto no caminho: camada-guia vetorial (0 = desliga), margem inicial,
    /// perpendicular, invertido.
    bool set_text_path(u64 layerId, u64 pathLayer, f32 offset, bool perpendicular, bool reverse) noexcept;
    bool query_text_path(u64 layerId, u64& pathLayer, f32& offset, bool& perpendicular, bool& reverse) noexcept;
    // --- Fontes ---------------------------------------------------------------------
    /// Fontes disponíveis (aparelho + importadas), por família e peso.
    [[nodiscard]] std::vector<text::FontEntry> list_fonts() noexcept;
    /// Registra um TTF/OTF já copiado para a pasta do app. Nulo se não for fonte.
    [[nodiscard]] Result<text::FontEntry> import_font(const char* path) noexcept;
    /// Fonte da camada de texto: família/peso/itálico e, se importada, o arquivo.
    bool set_text_font(u64 layerId, const std::string& family, u32 weight, bool italic, const std::string& path) noexcept;
    /// Estilo de parágrafo (18 floats): modo da caixa, largura, altura, fundo
    /// (liga, rgba, margem, raio), sombra (liga, rgba, dx, dy, desfoque).
    bool set_text_style(u64 layerId, const f32* in18) noexcept;
    bool query_text_style(u64 layerId, f32* out18) noexcept;
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
    /// Ajustes não animáveis (0..6 e as cores 28..35) de uma vez.
    bool set_text_animator(u64 layerId, u32 index, const f32* v40) noexcept;
    /// Valor de um parâmetro (TextAnimParam): com keyframes, grava no playhead.
    bool set_text_anim_param(u64 layerId, u32 index, u32 param, f32 value) noexcept;
    /// Liga/desliga o keyframe do parâmetro no playhead (losango).
    bool toggle_text_anim_key(u64 layerId, u32 index, u32 param) noexcept;
    /// Preset nativo (substitui os animadores), a partir do início da camada.
    bool apply_text_preset(u64 layerId, u32 preset) noexcept;

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

    /// Remapeamento de tempo: ligar cria a curva equivalente ao tempo atual
    /// (nada muda até editar); desligar volta à velocidade (a curva fica guardada).
    bool set_time_remap(u64 layerId, bool on) noexcept;
    /// Rampa de velocidade pronta sobre o trecho da fonte atual: 0 linear,
    /// 1 suave (entrada e saída), 2 herói (rápido-lento-rápido), 3 acelerar,
    /// 4 desacelerar. Liga o remapeamento.
    bool apply_speed_ramp(u64 layerId, u32 preset) noexcept;
    /// Curva de tempo para o editor de gráfico. `out` = {nº de pontos, início
    /// e fim locais da camada, último quadro da fonte (quadros da composição),
    /// velocidade no cabeçote} e depois 7 floats por ponto: tempo local, quadro
    /// da fonte, interpolação, bx1, by1, bx2, by2. Devolve os floats escritos.
    u32 query_time_remap(u64 layerId, f32* out, u32 maxFloats) noexcept;
    /// Ponto da curva: `index` < 0 insere em `localFrame` com o valor que a
    /// curva JÁ tem ali (nada muda até mover); senão move o ponto (tempo
    /// preso entre os vizinhos, valor dentro da fonte). `interp` < 0 mantém.
    /// Devolve o índice do ponto, ou −1.
    i32 edit_time_remap_key(u64 layerId, i32 index, i64 localFrame, f32 sourceFrame, i32 interp) noexcept;
    /// Apaga um ponto (ficam pelo menos dois).
    bool remove_time_remap_key(u64 layerId, u32 index) noexcept;

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
    /// Liga/desliga o key do caminho no cabeçote (`keyed` = ficou com key).
    bool toggle_mask_path_key(u64 layerId, u32 maskId, bool* keyed = nullptr) noexcept;
    /// Máscaras no cabeçote: [0..5] composição ← camada (a b c d tx ty), [6] nº
    /// de máscaras; por máscara kMaskHeaderFloats floats (id, modo, invertida,
    /// feather, expansão, opacidade, fechada, nº de pontos, nº de keys, key no
    /// cabeçote, ativa, 0) e 6 por ponto (a forma no cabeçote). Devolve os
    /// floats necessários; só escreve se `capacity` couber.
    static constexpr u32 kMaskHeaderFloats = 12;
    u32 query_masks(u64 layerId, f32* out, u32 capacity) noexcept;
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
    bool apply_particle_preset(u64 layerId, u32 preset) noexcept;
    /// 0 taxa, 1 vida, 2 velocidade, 3 espalhamento, 4 gravidade, 5 tamanho
    /// inicial, 6 tamanho final, 7 direção.
    bool set_particle_param(u64 layerId, u32 param, f32 value) noexcept;
    /// Os 8 parâmetros acima em `out`.
    bool query_particles(u64 layerId, f32* out8) noexcept;

    /// Desfoque de movimento da camada (liga também o da composição).
    bool set_motion_blur(u64 layerId, bool on) noexcept;
    /// Camada de ajuste: os efeitos dela valem para tudo o que está abaixo.
    bool set_layer_adjustment(u64 layerId, bool on) noexcept;
    /// Guia: aparece no preview e nunca sai no export.
    bool set_layer_guide(u64 layerId, bool on) noexcept;
    /// Etiqueta de cor (0 = nenhuma, até kLayerLabelCount − 1).
    bool set_layer_label(u64 layerId, u32 label) noexcept;
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
    };
    /// Analisa o vídeo da camada em segundo plano (0 rápido, 1 equilibrado,
    /// 2 alta qualidade). Uma análise por vez; a UI continua navegando.
    bool start_camera_track(u64 layerId, u32 mode) noexcept;
    /// Interrompe a análise (libera os quadros; o projeto não muda).
    void cancel_camera_track() noexcept;
    [[nodiscard]] CameraTrackStatus camera_track_status() noexcept;
    /// Cria a câmera rastreada (keyframes por quadro, FOV resolvida) e um Nulo
    /// 3D no chão da cena (plano dominante) ou no centro dos pontos. Devolve a
    /// câmera. A câmera antiga ativa é desativada (desfazível).
    [[nodiscard]] Result<u64> apply_camera_track() noexcept;
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
    bool query_gizmo(u64 layerId, f32 length, f32* out8) noexcept;
    /// Posição LOCAL (espaço do pai) que leva a camada `amount` unidades do
    /// mundo ao longo do eixo `axis` (0 X, 1 Y, 2 Z) a partir de onde está.
    bool gizmo_move_local(u64 layerId, u32 axis, f32 amount, f32* outXYZ) noexcept;

    // --- Ambiente 3D (HDRI) ------------------------------------------------------
    /// HDRI Radiance (.hdr) do arquivo local: ilumina e reflete nos modelos 3D
    /// da composição atual. Devolve o asset.
    [[nodiscard]] Result<u64> import_hdri(const char* path) noexcept;
    /// Volta ao estúdio neutro.
    bool clear_hdri() noexcept;
    /// Intensidade (≥ 0) e giro (graus) do ambiente.
    bool set_environment_params(f32 intensity, f32 rotationDeg) noexcept;
    /// {tem HDRI (0/1), intensidade, giro}.
    bool query_environment(f32* out3) noexcept;

    // --- Pré-composição ----------------------------------------------------------
    /// Move as camadas para uma composição nova (mesmo tamanho, taxa e
    /// duração; fundo transparente) e põe no lugar UMA camada que a mostra, na
    /// posição da mais alta. Os tempos não mudam. Devolve a camada nova.
    [[nodiscard]] Result<u64> precompose(const u64* ids, u32 count, const char* name = nullptr) noexcept;
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
    /// Área de transferência do motor (vive enquanto o app vive; colar em outro
    /// projeto só leva camadas cuja mídia exista lá).
    u32 copy_layers(const u64* ids, u32 count) noexcept;
    /// Cola no frame (o começo da mais cedo cai nele; as outras mantêm a
    /// distância). As coladas ficam escolhidas. Devolve quantas entraram.
    u32 paste_layers(i64 frame) noexcept;
    /// Estilo = mesclagem, opacidade, efeitos (trocados) e a aparência do tipo
    /// (texto: fonte/tamanho/cor/contorno; forma: preenchimento/contorno).
    bool copy_style(u64 layerId) noexcept;
    u32 paste_style(const u64* ids, u32 count) noexcept;
    /// Efeitos (com os keyframes deles): colar ACRESCENTA ao fim da pilha.
    u32 copy_effects(u64 layerId) noexcept;
    u32 paste_effects(const u64* ids, u32 count) noexcept;
    /// Keyframes no instante do frame (todas as propriedades com marca ali).
    u32 copy_keyframes(u64 layerId, i64 frame) noexcept;
    u32 paste_keyframes(const u64* ids, u32 count, i64 frame) noexcept;
    /// Bits: 1 camadas, 2 estilo, 4 efeitos, 8 keyframes.
    [[nodiscard]] u32 clipboard_state() noexcept;

    // --- Presets (formato em project/Presets.hpp) -------------------------------
    /// Preset `kind` (Effects, Text, Animation) da camada, em JSON. Vazio = a
    /// camada não tem o que salvar desse tipo. `parts` = TextPresetParts.
    [[nodiscard]] std::string save_preset(u64 layerId, presets::PresetKind kind, const std::string& name,
                                          u32 parts = presets::kTextAll) noexcept;
    /// Aplica o preset (um passo de desfazer). Efeitos ACRESCENTAM; texto troca
    /// estilo/animadores; animação começa no cabeçote (ou no início da camada
    /// se o cabeçote estiver fora dela) e, com `durationFrames` > 0, estica até
    /// essa duração. JSON inválido ou tipo que não serve à camada → false, sem
    /// mudar nada. `error` (opcional) diz o porquê.
    bool apply_preset(u64 layerId, const std::string& json, i64 durationFrames = 0, std::string* error = nullptr) noexcept;

    // --- Modo Edição (timeline magnética) ------------------------------------------
    void set_edit_mode(bool on) noexcept;
    [[nodiscard]] bool edit_mode() noexcept;
    /// Exclui as camadas e fecha só os buracos que a exclusão criou (um passo
    /// de desfazer). Vale em qualquer modo.
    bool ripple_delete(const u64* ids, u32 count) noexcept;
    /// Fecha todos os intervalos vazios da composição. Devolve os frames removidos.
    i64 remove_gaps() noexcept;
    /// Corta a composição no frame (duração = frame; camadas além são aparadas).
    bool trim_composition(i64 frame) noexcept;

    // --- Marcas e batidas -------------------------------------------------------
    /// Liga/desliga a marca da pessoa no frame (toggle). true = ficou marcada.
    bool toggle_marker(i64 frame) noexcept;
    bool move_marker(i64 from, i64 to) noexcept;
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

    /// Texto 3D: malha extrudada dos contornos da fonte, numa camada de
    /// modelo 3D centrada. Devolve a layer.
    [[nodiscard]] Result<u64> add_text3d(const scene3d::Text3DSpec& spec) noexcept;
    /// Troca texto/profundidade/cor/alinhamento (a malha é gerada de novo; desfazível).
    Status set_text3d(u64 layerId, const scene3d::Text3DSpec& spec) noexcept;
    /// Receita do texto 3D da camada (falso = não é texto 3D).
    bool query_text3d(u64 layerId, scene3d::Text3DSpec& out) noexcept;

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
    [[nodiscard]] Status render_offscreen(TextureHandle target, u32 width, u32 height, bool asPreview = false) noexcept;

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

    /// O frame do playhead em RGBA8 sRGB (alfa reto), com o lado maior em
    /// `maxDim`. Miniatura do projeto na Home. Síncrono (espera a GPU).
    [[nodiscard]] Status capture_frame_rgba(u32 maxDim, std::vector<u8>& out, u32& width, u32& height) noexcept;

    /// A PRÉVIA DE UM EFEITO (Fase 7.3): o efeito, com os valores padrão, sobre
    /// a cartela de demonstração. RGBA8 sRGB de alfa reto. Não depende de
    /// projeto nem de composição aberta — o navegador de efeitos existe antes
    /// de qualquer camada. `NotImplemented` = efeito sem prévia de um quadro.
    /// Foto de base das prévias de efeito (RGBA8 sRGB). O app manda uma vez.
    bool set_effect_preview_source(const u8* rgba, u32 width, u32 height) noexcept;
    [[nodiscard]] Status render_effect_preview(u32 typeId, u32 width, u32 height, std::vector<u8>& out,
                                               u32& outWidth, u32& outHeight) noexcept;

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
    bool query_composition(u64& id, u32& width, u32& height, f64& fps, i64& durationFrames,
                           f32 background[4]) noexcept;
    /// Teto de tamanho de composição do aparelho, como lado maior × lado
    /// menor (a mesma regra com que CompositionSetSize recusa).
    void composition_size_cap(u32& longSide, u32& shortSide) const noexcept;

    /// Detalhe de uma camada no playhead. false = camada não existe.
    bool query_layer_detail(u64 layerId, bridge::LayerDetailPOD& out) noexcept;
private:
    bool fill_layer_detail_locked(u64 layerId, bridge::LayerDetailPOD& out) noexcept;
public:
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

    /// Declaração dos parâmetros de um TIPO de efeito, sem precisar de layer:
    /// a ficha do catálogo (nome, tipo, faixa, unidade). `value` sai com o
    /// padrão da declaração e `animated` sai 0 — não há instância por trás.
    u32 query_effect_specs(u32 typeId, bridge::EffectParamRow* out, u32 capacity,
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
    };
    /// Bits de `ExportProgress::flags` (o mesmo valor vai para a UI).
    enum ExportFlag : u32 {
        kExportHardwareEncoder = 1u << 0,   ///< o sink confirmou encoder de hardware
        kExportSoftwareEncoder = 1u << 1,   ///< caiu para encoder de software (mais lento)
        kExportThermalReduced  = 1u << 2,   ///< calor: menos quadros em voo (qualidade igual)
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
    /// Maior lado de textura aceito ao importar modelo 3D — e o MESMO ao
    /// reabrir, senão o quadro muda entre importar e reabrir o projeto.
    [[nodiscard]] u32 model_texture_cap() const noexcept;
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
    StartupTimings     startup_{};
    std::atomic<EngineState> state_{EngineState::Uninitialized};
    Errc               lastError_ = Errc::Ok;
    char               lastErrorDetail_[128]{};
    /// Guarda o último erro (código padronizado + texto sem dado do usuário) e
    /// loga com o nome §115.
    void set_last_error(Status s, const char* context) noexcept;

    // --- Gravação / abertura (Fase 8G) ---------------------------------------
    std::mutex         saveMutex_;              ///< uma gravação por vez
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
    std::vector<CompositionId>    compStack_;   ///< caminho da principal até a aberta
    std::unordered_map<u64, std::shared_ptr<const scene3d::HdriPixels>> hdris_;
    struct CameraTrackJob;
    std::unique_ptr<CameraTrackJob> cameraTrack_;
    std::unordered_map<u64, std::shared_ptr<void>> cameraTrackCache_;
    void join_camera_track() noexcept;
    static std::shared_ptr<const scene3d::HdriPixels> hdri_lookup(void* self, AssetId id);
    struct Clipboard {
        std::vector<std::pair<u64, Layer>> layers;   ///< id original → cópia
        i64 layersAnchor = 0;
        bool hasStyle = false;
        Layer style;
        std::vector<EffectInstance> effects;
        std::vector<Track> effectTracks;            ///< EffectParam dos efeitos copiados
        struct Key { TrackProperty property; u32 effectIndex; u32 effectParamIndex; EffectTypeId effectType; Keyframe key; };
        std::vector<Key> keys;                       ///< tempo relativo ao instante copiado (0)
    } clipboard_;
    std::unordered_map<u64, ImagePixels> images_;   ///< por AssetId empacotado
    /// Modelos 3D carregados, por AssetId. Um por asset, qualquer número de
    /// layers (ModelInstance) apontando para ele.
    std::unordered_map<u64, std::shared_ptr<const scene3d::SceneAsset>> models_;
    static std::shared_ptr<const scene3d::SceneAsset> model_lookup(void* self, AssetId id);
    [[nodiscard]] std::string resolve_asset_path(const std::string& stored) const;
    [[nodiscard]] u32 caption_count_locked(const Composition& comp, u64 sourceLayer) const noexcept;
    void migrate_echo_to_effect() noexcept;
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
    std::atomic<u32>  lastCulledLayers_{0};       ///< telemetria (Fase 8C)
    std::atomic<u64>  renderWakeups_{0};

    SurfaceDesc surface_{};

    // --- Métricas ---------------------------------------------------------------
    mutable std::mutex perfMutex_;
    bridge::PerfPOD perf_{};
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
    std::unique_ptr<ExportContext> exportCtx_;
    /// Export em andamento: o render do preview não toca na GPU nem nos
    /// decoders (que o export usa em sequência).
    std::atomic<bool> exportActive_{false};
    /// O decoder entregou quadro: acorda o export que espera o quadro exato
    /// (sem dormir 5 ms às cegas por tentativa).
    std::mutex exportWakeMutex_;
    std::condition_variable exportWakeCv_;
    /// Calor (set_thermal): o export reduz quadros em voo, nunca a qualidade.
    std::atomic<bool> thermalDegrade_{false};
    void export_thread_main() noexcept;
    void export_encoder_main() noexcept;
    [[nodiscard]] Status render_export_frame(FrameIndex t, const OffscreenTarget& target, u64& gpuFrame) noexcept;
    [[nodiscard]] Status write_export_audio(i64 untilSample) noexcept;
    /// Âncora da camada de texto no centro da caixa atual (depois de editar).
    void recenter_text(Layer& l) noexcept;
};

} // namespace aurea
