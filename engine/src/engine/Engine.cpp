#include "aurea/Engine.hpp"
#include "aurea/scene3d/StudioEnvironment.hpp"
#include "aurea/text/LocalWhisper.hpp"
#include "aurea/audio/Beats.hpp"
#include "aurea/tracking/PointTracker.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/tracking/CameraTracker.hpp"
#include "aurea/tracking/CameraTrackData.hpp"
#include "aurea/tracking/TrackingValidation.hpp"
#include "../tracking/AnalysisDecode.hpp"

#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/text/TextTransform.hpp"
#include "aurea/timeline/LayerAnimator.hpp"
#include "aurea/vector/Vector.hpp"
#include "aurea/animation/KeyframeOptimize.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/ai/Upscaler.hpp"
#include "aurea/ai/TemporalStabilizer.hpp"
#include "aurea/export/BitratePolicy.hpp"
#include "aurea/export/UpscaleColor.hpp"
#include "aurea/effects/MotionTile.hpp"
#include "aurea/effects/Particular.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>

namespace aurea {

namespace {

/// O efeito é o Remapear tempo? (ele não desenha: quem age é a curva da camada)
[[nodiscard]] bool is_time_remap_type(EffectTypeId t) noexcept {
    return t == effect_type_id(effect_keys::kTimeRemap);
}

/// Liga a curva de remapeamento SEM passar pelo histórico — quem chama já está
/// dentro de uma mutação. Mesma curva que o painel de velocidade cria.
void enable_time_remap_curve(Layer& l) noexcept {
    if (l.timeRemapEnabled) return;
    if (l.timeRemap.keys.empty()) {
        const f64 s0 = l.source_frame(l.start), s1 = l.source_frame(l.end);
        l.timeRemap.property = TrackProperty::TimeRemap;
        l.timeRemap.set(l.local_time(l.start), static_cast<f32>(s0));
        l.timeRemap.set(l.local_time(l.end), static_cast<f32>(s1));
    }
    l.timeRemapEnabled = true;
}

bool is_remap_time_alias(const Layer& l, const TrackRef& ref) noexcept {
    if (ref.property != TrackProperty::EffectParam || ref.effectParamIndex != 0) return false;
    for (const EffectInstance& effect : l.effects)
        if (effect.id == ref.effectIndex) return is_time_remap_type(effect.type);
    return false;
}

const Track* read_command_track(const Layer& l, const TrackRef& ref) noexcept {
    if (ref.property == TrackProperty::TimeRemap || is_remap_time_alias(l, ref)) return &l.timeRemap;
    if (ref.property == TrackProperty::Speed) return l.tracks.find(ref.property);
    return l.tracks.find(ref.property, ref.effectIndex, ref.effectParamIndex);
}

Track* command_track(Layer& l, const TrackRef& ref, bool create) noexcept {
    if (ref.property == TrackProperty::MaskParam && (ref.effectParamIndex > 2 ||
        std::none_of(l.masks.begin(), l.masks.end(), [&](const Mask& m) { return m.id == ref.effectIndex; }))) return nullptr;
    if (ref.property == TrackProperty::TimeRemap || is_remap_time_alias(l, ref)) {
        if (create) enable_time_remap_curve(l);
        return &l.timeRemap;
    }
    // A velocidade é uma trilha só da layer (`Layer::speed_track` busca sem
    // índices): um comando que traga índice 0 não pode criar outra.
    if (ref.property == TrackProperty::Speed) return create ? &l.tracks.get_or_create(ref.property) : l.tracks.find(ref.property);
    if (create) return &l.tracks.get_or_create(ref.property, ref.effectIndex, ref.effectParamIndex);
    return l.tracks.find(ref.property, ref.effectIndex, ref.effectParamIndex);
}

/// Grava o tempo (segundos da FONTE) na curva, no instante local: atualiza a
/// chave que já existe ali ou cria uma. É o "mexer no valor" do efeito.
void write_time_remap(Layer& l, FrameIndex local, f32 sourceFrames, Interpolation interp) noexcept {
    const u32 at = l.timeRemap.find_exact(local);
    if (at == kInvalidIndex) l.timeRemap.set(local, sourceFrames, interp);
    else {
        l.timeRemap.keys[at].value = sourceFrames;
        l.timeRemap.keys[at].interp = interp;
    }
}

/// "Facilidade" do Remapear tempo (o enum do efeito) ↔ interpolação da chave.
/// 0 Linear · 1 Suave (entrada e saída) · 2 Segurar · 3 Suave no início ·
/// 4 Suave no fim. Os três primeiros são os números que os projetos antigos
/// gravaram — continuam querendo dizer a mesma coisa.
[[nodiscard]] Interpolation remap_interp_of_mode(f32 mode) noexcept {
    const i32 m = std::isfinite(mode) ? static_cast<i32>(std::lround(mode)) : 0;
    switch (m) {
        case 1: return Interpolation::EaseInOut;
        case 2: return Interpolation::Hold;
        case 3: return Interpolation::EaseIn;
        case 4: return Interpolation::EaseOut;
        default: return Interpolation::Linear;
    }
}
/// O caminho inverso. Curvas de Bézier feitas no gráfico antigo leem "Suave":
/// é o mais perto que a lista curta tem, e a curva só muda se alguém tocar.
[[nodiscard]] f32 remap_mode_of_interp(Interpolation i) noexcept {
    switch (i) {
        case Interpolation::Linear: return 0.0f;
        case Interpolation::Hold: return 2.0f;
        case Interpolation::EaseIn: return 3.0f;
        case Interpolation::EaseOut: return 4.0f;
        default: return 1.0f;
    }
}

} // namespace

namespace {
constexpr u32 kCameraTrackerVersion = 3;
using CameraTrackResult = tracking::CameraTrackData;
} // namespace

struct Engine::CameraTrackJob {
    std::thread thread;
    std::atomic<bool> cancel{false};
    std::atomic<f32> progress{0.0f};
    std::atomic<u32> state{0};
    std::atomic<bool> finished{true};
    std::mutex mutex;
    std::shared_ptr<const CameraTrackResult> result;
    std::string message;
    u64 layerId = 0;
    i64 start = 0;
    u64 cacheKey = 0;
    u64 session = 0;
    CompositionId composition{};
    AssetId source{};
    bool cached = false;
    std::vector<Vec3> appliedPoints;
    std::vector<u32> selectedTracks;
};

namespace {

/// Escreve uma string no blob da UI. Devolve false quando não cabe.
bool put_string(char* blob, u32 capacity, u32& cursor, const char* s, u32& outOffset, u32& outLength) {
    outOffset = 0;
    outLength = 0;
    if (!s) return true;
    const u32 len = static_cast<u32>(std::strlen(s));
    if (!blob || cursor + len > capacity) return false;
    std::memcpy(blob + cursor, s, len);
    outOffset = cursor;
    outLength = len;
    cursor += len;
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// ExportContext — uma sessão de export (threads, sink, alvos de GPU, progresso).
//
// PIPELINE SOBREPOSTO (Fase 8F). Três coisas andam ao mesmo tempo:
//
//   decoder  ── quadro N+1..N+3 (a thread de decode anda adiantada no modo Playback)
//   produtor ── prepara e grava o quadro N; a GPU renderiza, converte para NV12
//               e COPIA os planos para o buffer de leitura do slot, tudo num
//               frame só (sem submissão extra, sem fence próprio)
//   encoder  ── entrega o quadro N−1 ao sink direto do buffer mapeado + áudio
//
// Os slots (buffers de leitura) circulam produtor → encoder → produtor. A
// profundidade é o número de slots: limita a memória (3 × 1,5 × L × A bytes —
// 37 MB em 4K) e é o que a temperatura reduz (1 = serial). A GPU só é esperada
// no fence do quadro ANTERIOR, depois de submeter o atual.
// -----------------------------------------------------------------------------
namespace {
/// Preparos sem aparecer até um decoder sair durante o export (ver collect em
/// render_export_frame). Quem está na tela é tocado a cada preparo.
constexpr u32 kExportIdleFrames = 2;
} // namespace

struct Engine::ExportContext {
    mutable std::mutex mutex;          ///< protege `progress`
    ExportProgress progress{};
    ExportSettings settings{};
    std::string    outputPath;
    std::atomic<bool> cancelRequested{false};
    std::thread    thread;             ///< produtor (GPU)
    std::thread    encoder;            ///< consumidor (sink)
    std::unique_ptr<ExportSink> sink;

    // Plano da sessão, fixado no start (a timeline não muda durante o export:
    // o preview e os comandos da UI ficam congelados).
    u32 width = 0, height = 0;
    u32 outputWidth = 0, outputHeight = 0;
    f64 fps = 30.0;
    f64 compFps = 30.0;
    u32 frames = 0;
    bool dither = true;
    TextureHandle comp{}, y{}, uv{};
    // Áudio: o MESMO mixer do preview, com um cache próprio que decodifica
    // na hora (o export não disputa decoder com o preview).
    std::unique_ptr<audio::AudioBlockCache> audioCache;
    std::shared_ptr<audio::AudioMixSnapshot> audioSnap;
    /// Estado dos efeitos de áudio (atraso, reverb...) entre os pedaços.
    audio::MixState audioFx;
    i64 audioWritten = 0;
    std::vector<f32> audioMix;
    std::vector<i16> audioPcm;

    // --- Pipeline -------------------------------------------------------------
    struct Slot {
        BufferHandle y{}, uv{};
        const u8* yPtr = nullptr;      ///< mapeado persistente (invalidado após o fence)
        const u8* uvPtr = nullptr;
        u32 frame = 0;                 ///< índice de saída
        u64 gpuFrame = 0;              ///< frame do backend que escreveu o slot
    };
    std::vector<Slot> slots;
    u32 depth = 1;                     ///< slots em circulação (sem calor)
    std::mutex qMutex;                 ///< protege as duas filas e os estados abaixo
    std::condition_variable qCv;
    std::vector<u32> freeSlots;        ///< prontos para a GPU escrever
    std::deque<u32>  readySlots;       ///< planos prontos, em ordem de quadro (FIFO)
    bool producerDone = false;         ///< nada mais vai entrar em readySlots
    bool stop = false;                 ///< encerrar já (cancelamento ou erro)
    Status encoderStatus = OkStatus;   ///< erro do sink (escrito pelo encoder)
    bool encoderFailed = false;

    // Medição (ns acumulados na sessão; cada lado escreve só os seus).
    // Atômicos: o encoder publica as médias do produtor no progresso.
    std::atomic<u64> decodeNs{0}, renderNs{0}, readNs{0};   // produtor
    // Perfil (telemetria ligada): o que custa cada quadro, para medir no
    // aparelho de verdade. Só o produtor escreve; vai para o log no fim.
    struct PassSum { const char* label; f64 ms; };
    std::vector<PassSum> gpuPasses;    ///< ms somados por rótulo
    u32 gpuFrames = 0, profiledFrames = 0;
    f64 recordMs = 0.0, firstFrameMs = 0.0;
    std::atomic<u64> writeNs{0}, audioNs{0};                // encoder
    u64 startNs = 0;
    u32 thermalReducedFrames = 0;
    /// Quadros gravados com o quadro decodificado MAIS PRÓXIMO (o exato não
    /// veio a tempo). Só o produtor escreve.
    u64 fallbackFrames = 0;
    /// Depois de um fallback, a paciência cai até um quadro exato voltar: uma
    /// fonte quebrada não pode custar 4 s por quadro num export de horas.
    bool sourceDegraded = false;
    /// Tamanho estimado do arquivo e a pasta dele (conferência de espaço).
    u64 estimatedBytes = 0;
    std::filesystem::path outputDir;
    /// Export como imagem (start_image_export): sem sink nem planos NV12; o
    /// alvo `comp` é lido em RGBA16F e vira PNG/GIF/ZIP no motor.
    bool imageMode = false;
    ImageExportSettings image{};
    FrameIndex imageFrame{};   ///< PNG: o quadro do playhead no início
    bool imageAlpha = false;   ///< fundo transparente: PNG RGBA, GIF com transparência

    void set_message(const char* m) {
        std::snprintf(progress.message, sizeof(progress.message), "%s", m);
    }
};

/// Maior lado de textura de modelo 3D no celular: 2048 (uma 4K com mips são
/// ~90 MB de GPU; um personagem com cinco delas estoura a memória de um
/// aparelho médio). O MESMO teto no import e ao reabrir — o quadro não muda.
constexpr u32 kModelTextureCap = 2048;

/// Tradução das capacidades do BACKEND para o vocabulário de plataforma.
///
/// São duas structs de propósito: `render::GPUCapabilities` é o que o Vulkan
/// respondeu; `platform::GpuCapabilities` é o que o resto do motor consulta.
/// Preenche-se só o que as duas têm — campo sem correspondência fica no padrão,
/// porque inventar limite é pior que ficar no conservador.
GpuCapabilities device_gpu_from(const GPUCapabilities& g) noexcept {
    GpuCapabilities p;
    p.deviceName    = g.deviceName;
    p.driverVersion = g.driverInfo;
    p.vendorId      = g.vendorId;
    p.deviceId      = g.deviceId;
    // ES exposes compute starting at 3.1; older ES versions do not.
    p.vulkan        = g.apiName == "Vulkan";
    p.metal         = g.apiName == "Metal";
    p.openGLES      = g.apiName == "OpenGL ES";
    p.apiVersionMajor = g.apiMajor;
    p.apiVersionMinor = g.apiMinor;

    p.maxTextureSize            = g.maxTexture2D;
    p.maxComputeWorkgroupSize   = g.maxComputeWorkGroupInvocations;
    p.supportsCompute           = p.vulkan || p.metal ||
        (p.openGLES && (g.apiMajor > 3 || (g.apiMajor == 3 && g.apiMinor >= 1)));

    p.supportsFloat16           = g.fp16Arithmetic;
    p.supportsFloat16Storage    = g.fp16Storage;
    p.supportsDepthTexture      = g.depth32fSampled;
    p.supportsAnisotropicFiltering = g.maxSamplerAnisotropy > 1.0f;
    p.supportsAstcCompression   = g.textureCompressionASTC;
    p.supportsEtc2Compression   = g.textureCompressionETC2;
    p.supportsTimestampQueries  = g.timestampQueries;

    p.totalVideoMemoryBytes     = g.deviceLocalBytes;
    return p;
}

Engine::Engine() {
    commandQueue_ = std::make_unique<CommandQueue>();
    adaptive_ = new AdaptiveResolutionController(caps_);
    // Expressões lidas FORA de um quadro do renderer (consultas da UI, que já
    // seguram o lock do modelo) acham a camada dona pela timeline deste motor.
    expr::register_provider([](void* self) -> const Timeline* {
        const Engine* e = static_cast<const Engine*>(self);
        return e->project_ ? &e->project_->timeline() : nullptr;
    }, this);
}

Engine::~Engine() {
    expr::unregister_provider(this);
    shutdown();
    delete adaptive_;
    adaptive_ = nullptr;
}

// =============================================================================
// Ciclo de vida
// =============================================================================
Status Engine::initialize(const EngineConfig& config) noexcept {
    if (state_ != EngineState::Uninitialized) {
        return Status{Errc::InvalidState, "motor ja inicializado"};
    }
    config_ = config;
    renderer_.set_foreground_model_directory(config.documentsDirectory + "/ai/rotobrush");
    startup_ = StartupTimings{};
    const u64 tStart = monotonic_ns();
    auto ms_since = [](u64 t0) noexcept { return static_cast<f32>(static_cast<f64>(monotonic_ns() - t0) * 1e-6); };
    // A medição da plataforma entra ANTES da detecção: o que ela traz é medido
    // no aparelho, e o que a detecção genérica não alcança (codec de hardware,
    // núcleos grandes) fica com o conservador.
    if (config_.hasPlatformInfo) caps_.apply_platform_info(config_.platformInfo);
    caps_.detect();

    const u32 workers = config.workerCount ? config.workerCount : caps_.recommended_worker_count();
    if (const Status s = jobs_.start(workers); !s.ok()) {
        lastError_ = s.code();
        state_ = EngineState::Failed;
        return s;
    }

    apply_memory_budgets();

    if (effectRegistry_.count() == 0) register_builtin_effects(effectRegistry_);
    startup_.jobsMs = ms_since(tStart);

    gpu_.reset(config.backend);
    renderer_.set_model_lookup(&Engine::model_lookup, this);
    renderer_.set_hdri_lookup(&Engine::hdri_lookup, this);
    renderer_.set_cube_lookup(&Engine::cube_lookup, this);
    if (gpu_) {
        // O backend guarda o caminho do cache de pipeline: por padrão, o mesmo
        // diretório de cache do motor (que vive em config_, não no chamador).
        BackendConfig bc = config_.backendConfig;
        if (!bc.cacheDirectory || !*bc.cacheDirectory) bc.cacheDirectory = config_.cacheDirectory.c_str();
        // Versão do cache: SPIR-V mudou (app atualizado) → arquivo antigo fora.
        if (bc.pipelineCacheTag == 0) bc.pipelineCacheTag = ShaderLibrary::spirv_fingerprint();
        const u64 tGpu = monotonic_ns();
        const Status gs = gpu_->initialize(bc);
        startup_.gpuMs = ms_since(tGpu);
        const u64 tRenderer = monotonic_ns();
        if (!gs.ok()) {
            set_last_error(gs, "inicializar GPU");
            AUREA_LOG_ERROR("backend grafico nao inicializou: %s", gs.message().data());
            gpu_.reset();
        } else if (const Status r = renderer_.initialize(*gpu_, effectRegistry_); !r.ok()) {
            set_last_error(r, "inicializar renderer");
            AUREA_LOG_ERROR("renderer nao inicializou: %s", r.message().data());
            renderer_.shutdown();
            gpu_->shutdown();
            gpu_.reset();
        } else {
            AUREA_LOG_INFO("GPU: %s", gpu_->capabilities().summary().c_str());
        }
        startup_.rendererMs = ms_since(tRenderer);
        startup_.pipelinesPrewarmed = renderer_.pipelines_prewarmed();
    }
    // A GPU de verdade só passa a existir agora: o backend acabou de subir e é
    // o único que sabe os limites reais. É aqui que "gpu=desconhecida
    // max_textura=2048" vira o aparelho que está na mão — e, como o orçamento
    // de memória sai daí, ele é refeito com os números medidos.
    if (gpu_ && caps_.apply_gpu(device_gpu_from(gpu_->capabilities()))) {
        apply_memory_budgets();
        AUREA_LOG_INFO("dispositivo (GPU medida): %s", caps_.summary().c_str());
    }

    text::set_default_font_path(config.defaultFontPath);
    // Fonte importada guardada como "docs:…": o gerenciador resolve pelo motor.
    text::FontManager::instance().set_path_resolver([this](const std::string& s) { return resolve_asset_path(s); });
    if (!config.documentsDirectory.empty()) {
        // Onde cada app copia a fonte importada: Android docs/fontes, iOS docs/Media.
        text::FontManager::instance().set_imported_dirs({config.documentsDirectory + "/fontes",
                                                         config.documentsDirectory + "/Media"});
    }
    media_.set_factory(config.mediaFactory);
    media_.set_memory(&memory_);
    media_.set_ready_callback(&Engine::on_frame_ready, this);
    media_.proxies().configure(config.mediaFactory, config.exportSinkFactory, config.exportSinkContext,
        config.cacheDirectory.empty() ? std::string{} : config.cacheDirectory + "/preview-proxies", 512ull << 20,
        [](const std::string& path, void* context) { return static_cast<Engine*>(context)->resolve_asset_path(path); }, this);
    media_.proxies().set_policy(caps_.policy().preferProxyAboveShortSide, caps_.policy().backgroundPauseMs);
    media_.proxies().set_pause_reason(PreviewProxyService::Thermal, caps_.thermal().severe());
    // Som: cache de blocos na verba de áudio; com saída, o áudio passa a ser o
    // relógio mestre do playback.
    audio_.initialize(config.mediaFactory, config.audioOutput, memory_.budget(MemoryClass::Audio));
    playback_.clock().set_master(&audio_);
    // Forma de onda / Espectro de áudio leem os MESMOS blocos que tocam.
    renderer_.set_audio_source(audio_.cache(), &Engine::audio_path_resolver, this);
    waveforms_ = std::make_unique<audio::WaveformCache>(config.mediaFactory);
    // Picos guardados em disco: reabrir o projeto não decodifica o áudio de novo (8D).
    if (!config_.cacheDirectory.empty()) waveforms_->set_disk_directory(config_.cacheDirectory + "/waveform");
    waveforms_->attach(&memory_);
    thumbs_.set_factory(config.mediaFactory);
    thumbs_.attach(&memory_);
    if (config.mediaFactory) thumbs_.start();

    adapt().configure(1920, 1080, config.displayRefreshRate);
    adapt().set_user_scale(config.initialPreviewScale);

    state_ = EngineState::Ready;
    startup_.totalMs = ms_since(tStart);
    startup_.restMs = std::max(0.0f, startup_.totalMs - startup_.jobsMs - startup_.gpuMs - startup_.rendererMs);
    AUREA_LOG_INFO("Aurea Engine pronta: %s", caps_.summary().c_str());
    AUREA_LOG_INFO("abertura do motor: %.1f ms (aparelho %.1f, gpu %.1f, renderer %.1f com %u pipelines, resto %.1f)",
                   startup_.totalMs, startup_.jobsMs, startup_.gpuMs, startup_.rendererMs,
                   startup_.pipelinesPrewarmed, startup_.restMs);
    return OkStatus;
}

u32 Engine::model_texture_cap() const noexcept {
    // O teto do celular manda, e o do aparelho é o piso: uma GPU que só aceita
    // 4096 num 2D não pode receber 2048 com mips em cada eixo. Os dois sítios
    // (importar agora e reabrir depois) passam AQUI — era essa a regra do
    // `kModelTextureCap`, e ela estava quebrada: o import usava 4096 fixo e a
    // reabertura usava 2048, então o mesmo .aurea abria diferente de como foi
    // importado.
    const u32 gpu = caps_.max_texture_dimension();
    return gpu ? std::min(kModelTextureCap, gpu) : kModelTextureCap;
}

void Engine::apply_memory_budgets() noexcept {
    // Uma tabela só (kBudgetShare, em MemoryManager.hpp): cada consumidor com o
    // seu pedaço do orçamento medido do aparelho. Ver PHASE_8_REPORT §8B.
    const u64 budget = DeviceCapabilities::process_budget_limit(
        config_.memoryBudgetBytes ? config_.memoryBudgetBytes : caps_.memory_budget_bytes());
    memory_.apply_budget_table(budget);
    // The rendered-frame category also contains flow/LUT/mask caches. Reserve
    // half for reusable intermediate render targets instead of retaining every
    // preview/export resolution until the frame-age timeout.
    renderer_.set_transient_cache_budget(memory_.budget(MemoryClass::RenderedFrames) / 2);
    // Aparelho de entrada (§107): cache de decode menor que o da tabela
    // (a fatia da tabela vale para o plano de sempre, 24 %).
    if (const u32 pct = caps_.policy().decodedFramesBudgetPercent; pct != 24) {
        memory_.set_budget(MemoryClass::DecodedFrames, budget * pct / 100);
    }
    // Desfazer (§126): 1/16 do orçamento, entre 16 e 128 MB.
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        history_.set_budget_bytes(std::clamp<u64>(budget / 16, 16ull << 20, 128ull << 20));
    }
}

void Engine::shutdown() noexcept {
    if (state_ == EngineState::Uninitialized || state_ == EngineState::ShuttingDown) return;
    state_ = EngineState::ShuttingDown;

    if (exportCtx_ && exportCtx_->thread.joinable()) {
        (void)cancel_export();   // marca e ACORDA as esperas do pipeline
        exportCtx_->thread.join();
    }
    join_camera_track();
    join_motion_track();
    text::FontManager::instance().set_path_resolver(nullptr);
    stop_render_thread();
    media_.proxies().stop();
    thumbs_.stop();
    thumbs_.clear();
    thumbs_.attach(nullptr);
    playback_.clock().set_master(nullptr);
    renderer_.set_audio_source(nullptr, nullptr, nullptr);
    audio_.shutdown();
    waveforms_.reset();
    media_.close_all();
    media_.set_memory(nullptr);
    jobs_.stop();

    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        if (gpu_) {
            gpu_->wait_idle();
            renderer_.shutdown();
            if (surfaceAttached_) gpu_->detach_surface();
            surfaceAttached_ = false;
            gpu_->save_pipeline_cache();
            gpu_->shutdown();
            gpu_.reset();
        }
    }

    { std::lock_guard<std::mutex> commands(commandSubmitMutex_); commandQueue_->reset(); }
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        project_.reset();
        ++projectSession_;
        images_.clear();
        models_.clear();
        hdris_.clear();
    cubeLuts_.clear();
    }
    state_ = EngineState::Uninitialized;
}

EngineState Engine::state() const noexcept { return state_; }

Status Engine::suspend() noexcept {
    const EngineState s = state_;
    if (s != EngineState::Ready && s != EngineState::Rendering) {
        return Status{Errc::InvalidState, "motor nao esta pronto"};
    }
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        playback_.pause(monotonic_ns());
        playingHint_ = false;
        audio_.stop();
    }
    // Decoders de hardware são recurso do SISTEMA: segurar em segundo plano
    // faz outro app (ou o próprio Aurea ao voltar) falhar ao abrir um codec.
    media_.suspend_all();
    media_.proxies().set_pause_reason(PreviewProxyService::Background, true);
    media_.proxies().set_pause_reason(PreviewProxyService::Playback, false);
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        if (gpu_) {
            gpu_->wait_idle();
            gpu_->save_pipeline_cache();
        }
    }
    state_ = EngineState::Suspended;
    return OkStatus;
}

MemoryManager::TrimReport Engine::trim_memory(i32 osLevel) noexcept {
    const TrimStage upTo = trim_stage_for_os_level(osLevel);
    if (upTo == TrimStage::None || state_ == EngineState::Uninitialized) return MemoryManager::TrimReport{};
    // 1–3 (e o que mais estiver registrado): os caches de CPU, cada um com o
    // próprio lock. O projeto (Persistent) não é registrável.
    (void)memory_.trim(upTo);
    const u8 st = static_cast<u8>(upTo);
    // 4–6: o cache de render e os assets 3D são do renderer, sob o lock de
    // render (entre quadros). Com export rodando, a GPU é dele: fica para o
    // próximo aviso do sistema.
    if (st >= static_cast<u8>(TrimStage::OldRenderCache) && !exportActive_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> rl(renderMutex_);
        if (gpu_ && renderer_.ready()) {
            gpu_->wait_idle();   // fora do caminho quente: é um aviso do sistema
            const u64 before = gpu_->memory_stats().usedBytes;
            const u32 textures = renderer_.trim_memory(st, frameCounter_);
            gpu_->wait_idle();   // roda as destruições adiadas até o fence
            const u64 after = gpu_->memory_stats().usedBytes;
            memory_.note_trim_freed(TrimStage::OldRenderCache, before > after ? before - after : 0);
            AUREA_LOG_INFO("memoria: renderer soltou %u texturas (%llu KB de GPU)", textures,
                           static_cast<unsigned long long>((before > after ? before - after : 0) / 1024));
        }
    }
    // 7: temporários — fontes de vídeo que não entraram no último quadro
    // (decoder + thread + buffers); reabrem sozinhas quando voltarem à tela.
    if (st >= static_cast<u8>(TrimStage::Temporaries) && !exportActive_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> rl(renderMutex_);
        const u32 before = media_.stats().sources;
        const usize framesBefore = memory_.used(MemoryClass::DecodedFrames);
        media_.collect(frameCounter_, 1);
        const usize framesAfter = memory_.used(MemoryClass::DecodedFrames);
        memory_.note_trim_freed(TrimStage::Temporaries, framesBefore > framesAfter ? framesBefore - framesAfter : 0);
        AUREA_LOG_INFO("memoria: %u fonte(s) de video ociosa(s) encaminhada(s) para fechamento", before - media_.stats().sources);
    }
    return memory_.last_trim();
}

Status Engine::resume() noexcept {
    if (state_ != EngineState::Suspended) return Status{Errc::InvalidState, "motor nao esta suspenso"};
    media_.resume_all();
    media_.proxies().set_pause_reason(PreviewProxyService::Background, false);
    state_ = EngineState::Ready;
    invalidate();
    return OkStatus;
}

void Engine::invalidate() noexcept {
    forceRender_.store(true, std::memory_order_release);
    request_render();
}

Status Engine::attach_surface(void* nativeWindow, u32 width, u32 height) noexcept {
    std::lock_guard<std::mutex> rl(renderMutex_);
    if (!gpu_) return Status{Errc::NotSupported, "sem backend grafico"};
    pendingSurfaceSize_.store(0, std::memory_order_release);
    surface_.nativeWindow = nativeWindow;
    surface_.width = width;
    surface_.height = height;
    surface_.vsync = true;
    const Status s = gpu_->attach_surface(surface_);
    surfaceAttached_ = s.ok();
    // Superfície nova (voltou do seletor de mídia, girou a tela) começa vazia:
    // redesenha mesmo que o modelo não tenha mudado.
    if (s.ok()) {
        forceRender_.store(true, std::memory_order_release);
        request_render();
    }
    return s;
}

void Engine::detach_surface() noexcept {
    std::lock_guard<std::mutex> rl(renderMutex_);
    surfaceAttached_ = false;
    pendingSurfaceSize_.store(0, std::memory_order_release);
    if (gpu_) gpu_->detach_surface();
    surface_.nativeWindow = nullptr;
}

Status Engine::resize_surface(u32 width, u32 height) noexcept {
    if (!width || !height) return Errc::InvalidArgument;
    // SurfaceView/layout callbacks run on the UI thread. Never wait there
    // for an in-flight render, decoder or GPU fence; coalesce to the latest size.
    pendingSurfaceSize_.store((static_cast<u64>(width) << 32) | height, std::memory_order_release);
    request_render();
    return OkStatus;
}

// =============================================================================
// Thread de render
// =============================================================================
void Engine::start_render_thread() noexcept {
    if (renderRunning_.exchange(true)) return;
    renderThread_ = std::thread([this] { render_thread_main(); });
}

void Engine::stop_render_thread() noexcept {
    if (!renderRunning_.exchange(false)) return;
    request_render();
    if (renderThread_.joinable()) renderThread_.join();
}

void Engine::request_render() noexcept {
    forceRender_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeFlag_ = true;
    }
    wakeCv_.notify_one();
}

void Engine::wake_render() noexcept {
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeFlag_ = true;
    }
    wakeCv_.notify_one();
}

void Engine::on_frame_ready(void* self) {
    // Thread de decode: só acorda o render. Nada de lock do modelo aqui. O
    // frame novo só força redesenho se o último frame mostrado estava
    // incompleto; durante o playback o decode anda adiantado e cada frame
    // pronto NÃO vale um redesenho.
    auto* e = static_cast<Engine*>(self);
    e->mediaReadyGen_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(e->wakeMutex_);
        e->wakeFlag_ = true;
    }
    e->wakeCv_.notify_one();
    if (e->exportActive_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(e->exportWakeMutex_);
        e->exportWakeCv_.notify_all();
    }
}

void Engine::render_thread_main() noexcept {
    set_current_thread_name("aurea-render");
    set_current_thread_priority(ThreadPriority::Display);
    while (renderRunning_) {
        {
            std::unique_lock<std::mutex> lock(wakeMutex_);
            if (playingHint_.load() && !lastSkipped_) {
                // Acabou de apresentar: o próximo frame pode já estar devido
                // (a aquisição FIFO do swapchain dá o ritmo do vsync).
            } else if (playingHint_.load()) {
                // Tocando, mas o frame do playhead ainda não mudou: dorme até
                // ele mudar (ou até a UI/decoder acordar).
                const u64 now = monotonic_ns();
                const u64 due = nextFrameDueNs_ > now ? nextFrameDueNs_ - now : 0;
                const u64 waitNs = std::clamp<u64>(due, 500'000ull, 50'000'000ull);
                wakeCv_.wait_for(lock, std::chrono::nanoseconds(waitNs), [this] {
                    return !renderRunning_ || wakeFlag_;
                });
            } else if (!surfaceAttached_ || state_ == EngineState::Suspended) {
                // Sem onde desenhar (segundo plano, tela bloqueada, superfície
                // ainda não chegou): nada para fazer até alguém acordar. Quem
                // devolve a tela — attach_surface, resume — já chama
                // request_render. Antes este caso também acordava a cada
                // 500 ms só para ver que não havia superfície (§38).
                wakeCv_.wait(lock, [this] { return !renderRunning_ || wakeFlag_; });
            } else if (refinePending_) {
                // CADisplayLink wakes us every vsync on iOS. A relative wait
                // restarted by each wake would prevent refinement forever.
                const u64 now = monotonic_ns();
                const u64 remaining = refineDueNs_ > now ? refineDueNs_ - now : 0;
                wakeCv_.wait_for(lock, std::chrono::nanoseconds(remaining), [this] {
                    return !renderRunning_ || wakeFlag_ || playingHint_.load();
                });
                // Actual edits restart the settling interval; an idle vsync doesn't.
                if (!playingHint_.load() && !forceRender_.load(std::memory_order_acquire)
                    && monotonic_ns() >= refineDueNs_) refineNow_ = true;
            } else {
                // Parado: dorme até ter o que mostrar. O teto de 500 ms é a
                // rede das mudanças do modelo que sobem `modelRevision_` sem
                // chamar request_render (a render_frame pula barato quando
                // nada mudou).
                wakeCv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
                    return !renderRunning_ || wakeFlag_ || playingHint_.load();
                });
            }
            ++renderWakeups_;
            wakeFlag_ = false;
        }
        if (!renderRunning_) break;
        if (!surfaceAttached_ || state_ == EngineState::Suspended) continue;
        if (refineNow_) {
            forceRender_.store(true, std::memory_order_release);
            (void)render_frame(true);
            refineNow_ = false;
            continue;
        }
        (void)render_frame(true);
    }
}

// =============================================================================
// Projeto
// =============================================================================
Status Engine::new_project(u32 width, u32 height, f64 fps, const char* title) noexcept {
    auto result = Project::create_new(width, height, fps,
                                      title ? std::string(title) : std::string("Projeto sem titulo"));
    if (!result.ok()) { lastError_ = result.code(); return result.status(); }

    join_camera_track();
    join_motion_track();
    cameraTrack_.reset();
    motionTrack_.reset();
    media_.close_all();
    thumbs_.clear();
    media_.proxies().clear();
    if (waveforms_) waveforms_->clear();
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        renderer_.release_project_resources();
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    rawPlaybackLayer_ = {};
    // Handles are reused by the new project. Edits queued for the previous
    // session must never be replayed against its layers.
    {
        std::lock_guard<std::mutex> commands(commandSubmitMutex_);
        commandQueue_->reset();
        project_ = std::make_unique<Project>(std::move(*result));
    }
    ++projectSession_;
    compStack_.clear();
    images_.clear();
    models_.clear();
    hdris_.clear();
    cubeLuts_.clear();
    history_.clear();
        modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    selection_.clear();
    if (Composition* c = current_composition()) {
        adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
        playback_ = PlaybackController{};
        playback_.configure(c->fps(), c->duration());
    }
    frameScheduler_.reset();
    state_ = EngineState::Ready;
    request_render();
    return OkStatus;
}

/// Eco antigo (campos da camada) → efeito "Eco e rastro": o painel de eco
/// saiu; o efeito é o lugar do eco agora (com keyframes). Mesmo resultado.
void Engine::migrate_echo_to_effect() noexcept {
    const EffectTypeId type = effectRegistry_.find_key(effect_keys::kEchoTrail);
    const ParameterRegistry* params = effectRegistry_.params(type);
    if (!params || params->count() < 4) return;
    project_->timeline().for_each_composition([&](CompositionId, Composition& c) {
        for (u32 i = 0; i < c.order().size(); ++i) {
            Layer* l = c.layer(c.order().at(i));
            if (!l || (l->echoCount == 0 && l->rgbDelay <= 0.0f) || l->effects.size() >= kMaxEffectCount) continue;
            EffectInstance e;
            e.id = l->alloc_effect_id();
            e.type = type;
            initialize_instance(e, *params);
            e.params[0].constant = ParamValue::scalar(static_cast<f32>(l->echoCount));
            e.params[1].constant = ParamValue::scalar(l->echoDelay);
            e.params[2].constant = ParamValue::scalar(l->echoDecay);
            e.params[3].constant = ParamValue::scalar(l->rgbDelay);
            l->effects.push_back(std::move(e));
            l->echoCount = 0;
            l->rgbDelay = 0.0f;
        }
    });
}

void Engine::set_last_error(Status s, const char* context) noexcept {
    lastError_ = s.code();
    std::snprintf(lastErrorDetail_, sizeof(lastErrorDetail_), "%s", s.detail().empty() ? "" : s.detail().data());
    if (!s.ok()) {
        // Nome padronizado + número + contexto fixo: nada de caminho nem de
        // título do usuário no log (§113).
        AUREA_LOG_ERROR("%s: %.*s (%d) %.*s", context ? context : "erro",
                        static_cast<int>(error_code_name(s.code()).size()), error_code_name(s.code()).data(),
                        s.raw(), static_cast<int>(s.detail().size()), s.detail().data());
    }
}

namespace {

/// iOS: a pasta do app (`…/Data/Application/<UUID>/Documents`) pode mudar de
/// nome numa atualização. Um caminho absoluto gravado pela instalação antiga
/// que não existe mais vira o MESMO `<relativo>` dentro da Documents atual —
/// só se o arquivo existir lá. Vazio = não se aplica (fica como estava e a UI
/// avisa "mídia ausente"). Nunca sai da Documents atual.
std::string rebase_moved_documents(const std::string& stored, const std::string& docs) {
    if (docs.empty() || stored.size() < 2 || stored[0] != '/') return {};
    constexpr std::string_view kContainer = "/Data/Application/";
    constexpr std::string_view kDocuments = "/Documents/";
    const usize at = stored.find(kContainer);
    if (at == std::string::npos) return {};
    const usize docsAt = stored.find(kDocuments, at + kContainer.size());
    if (docsAt == std::string::npos) return {};
    const std::string rel = stored.substr(docsAt + kDocuments.size());
    if (rel.empty() || rel.find("..") != std::string::npos) return {};
    std::string candidate = docs;
    if (candidate.back() != '/') candidate += '/';
    candidate += rel;
    if (candidate == stored) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(fs::u8path(stored), ec)) return {};
    ec.clear();
    if (!fs::is_regular_file(fs::u8path(candidate), ec) || ec) return {};
    return candidate;
}

/// Aplica [rebase_moved_documents] a toda mídia e fonte do projeto aberto.
u32 rebase_project_paths(Project& project, const std::string& docs) {
    if (docs.empty()) return 0;
    u32 moved = 0;
    project.for_each_asset([&](AssetId, Asset& a) {
        std::string r = rebase_moved_documents(a.sourcePath, docs);
        if (!r.empty()) { a.sourcePath = std::move(r); ++moved; }
    });
    project.timeline().for_each_composition([&](CompositionId, Composition& c) {
        for (u32 i = 0; i < c.order().size(); ++i) {
            Layer* l = c.layer(c.order().at(i));
            if (!l || l->kind != LayerKind::Text) continue;
            std::string r = rebase_moved_documents(l->text.fontPath, docs);
            if (!r.empty()) { l->text.fontPath = std::move(r); ++moved; }
        }
    });
    return moved;
}

/// Abertura estrita: só vale se TODAS as seções vieram (checksum, versão).
bool load_strict(const std::string& path, Project& out, LoadReport& report, Status& status) {
    LoadOptions o;
    o.lazyAssets = true;
    o.tolerateCorruptSections = false;
    std::string err;
    status = ProjectSerializer::load(out, path, o, &report, &err);
    if (status.ok() && report.partial) status = Status{Errc::CorruptData, "secoes faltando"};
    return status.ok();
}

} // namespace

Status Engine::load_project(const char* path) noexcept {
    if (!path || !*path) return Errc::InvalidArgument;
    const std::string main = path;
    u32 notice = 0;

    // 1. O principal, estrito.
    Project loaded;
    LoadReport report;
    Status mainStatus;
    std::string openedFrom = main;
    bool ok = load_strict(main, loaded, report, mainStatus);

    // Versão futura: recusa sem tentar cópias. Abrir um .bak mais velho e
    // salvar por cima apagaria o trabalho feito na versão nova.
    if (!ok && mainStatus.code() == Errc::UnsupportedVersion) {
        set_last_error(mainStatus, "abrir projeto");
        return mainStatus;
    }

    // 2. Principal ilegível: o último estado válido. O `.tmp` só sobra quando
    //    a queda foi entre o fsync e o rename — é o mais novo; o `.bak` é a
    //    gravação anterior.
    if (!ok) {
        for (const std::string& candidate : {fileio::temp_path(main), fileio::backup_path(main)}) {
            if (!fileio::exists(candidate)) continue;
            Project p;
            LoadReport r;
            Status s;
            if (load_strict(candidate, p, r, s)) {
                loaded = std::move(p);
                report = r;
                openedFrom = candidate;
                notice |= kLoadRecoveredCopy;
                ok = true;
                AUREA_LOG_WARN("projeto: principal ilegivel (%.*s); aberto da copia %s",
                               static_cast<int>(error_code_name(mainStatus.code()).size()),
                               error_code_name(mainStatus.code()).data(),
                               candidate.size() > 4 && candidate.compare(candidate.size() - 4, 4, ".bak") == 0 ? ".bak" : ".tmp");
                break;
            }
        }
    }

    // 3. Nenhuma cópia inteira: abre o que der do principal, desde que a
    //    timeline tenha vindo (sem ela seria um projeto vazio fingindo ser o
    //    do usuário).
    if (!ok && mainStatus.code() != Errc::NotFound) {
        Project p;
        LoadReport r;
        LoadOptions o;
        o.lazyAssets = true;
        o.tolerateCorruptSections = true;
        const Status s = ProjectSerializer::load(p, main, o, &r, nullptr);
        const bool hasTimeline = std::find(r.sectionsRead.begin(), r.sectionsRead.end(), SectionKind::Timeline)
                                 != r.sectionsRead.end();
        if (s.ok() && hasTimeline) {
            loaded = std::move(p);
            report = r;
            notice |= kLoadPartial;
            ok = true;
        }
    }

    if (!ok) {
        const Status err = mainStatus.code() == Errc::NotFound
                               ? Status{Errc::NotFound, "arquivo do projeto nao encontrado"}
                               : Status{Errc::ProjectCorrupted, "nenhuma copia valida do projeto"};
        set_last_error(err, "abrir projeto");
        return err;
    }

    // O principal ruim NÃO é apagado nem sobrescrito às cegas: vai para
    // `.corrompido` (uma cópia) antes que a próxima gravação o substitua.
    //    Sem essa cópia a abertura é recusada: a próxima gravação substituiria
    //    o único original (disco cheio aqui = disco cheio no salvar também).
    //    Nunca sobrescreve um `.corrompido` anterior.
    if ((notice & (kLoadRecoveredCopy | kLoadPartial)) && fileio::exists(main)) {
        std::string keep = main + ".corrompido";
        for (int i = 2; fileio::exists(keep) && i < 100; ++i) keep = main + ".corrompido" + std::to_string(i);
        const Status c = fileio::copy_file(main, keep);
        if (!c.ok()) {
            AUREA_LOG_WARN("copia do principal ruim nao gravada (%d): abertura recusada", c.raw());
            const Status err{c.code() == Errc::StorageFull ? Errc::StorageFull : Errc::IoError,
                             "sem espaco para guardar o original antes de abrir"};
            set_last_error(err, "abrir projeto");
            return err;
        }
    }
    // Formato antigo (§123–124): cópia de recuperação ANTES de qualquer
    // regravação no formato novo. Uma por versão de origem; não sobrescreve.
    if (report.olderFormat) {
        notice |= kLoadOlderFormat;
        const std::string copy = main + ".v" + std::to_string(report.timelineVersion) + ".bak";
        if (!fileio::exists(copy)) {
            // Sem a cópia, a primeira gravação no formato novo seria o fim do
            // original: recusa abrir (o arquivo fica intocado) em vez de arriscar.
            const Status c = fileio::copy_file(openedFrom, copy);
            if (!c.ok()) {
                AUREA_LOG_WARN("copia do formato antigo nao gravada (%d): abertura recusada", c.raw());
                const Status err{c.code() == Errc::StorageFull ? Errc::StorageFull : Errc::IoError,
                                 "sem espaco para guardar o formato antigo antes de abrir"};
                set_last_error(err, "abrir projeto");
                return err;
            }
            AUREA_LOG_INFO("formato antigo (timeline v%u): copia de recuperacao guardada", report.timelineVersion);
        }
    }

    if (const u32 moved = rebase_project_paths(loaded, config_.documentsDirectory)) {
        AUREA_LOG_INFO("projeto: %u caminho(s) da instalacao anterior religados a pasta atual", moved);
    }

    join_camera_track();
    join_motion_track();
    cameraTrack_.reset();
    motionTrack_.reset();
    media_.close_all();
    thumbs_.clear();
    media_.proxies().clear();
    if (waveforms_) waveforms_->clear();
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        renderer_.release_project_resources();
    }
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        rawPlaybackLayer_ = {};
        {
            std::lock_guard<std::mutex> commands(commandSubmitMutex_);
            commandQueue_->reset();
            project_ = std::make_unique<Project>(std::move(loaded));
        }
        ++projectSession_;
        // The saved current composition is only an editor view. Its navigation
        // stack is not serialized, so reopening it would strand the user in a
        // child and make the rest of the project appear lost after a crash.
        compStack_.clear();
        project_->timeline().set_current(project_->timeline().root());
        project_->set_path(main);
        mainFileSuspect_ = (notice & (kLoadRecoveredCopy | kLoadPartial)) != 0;
        migrate_echo_to_effect();
        // Append newly introduced parameters without resetting old values,
        // expressions or keyframe addresses. Old projects otherwise display
        // the new controls but EffectSetParam rejects their missing slots.
        project_->timeline().for_each_composition([&](CompositionId, Composition& c) {
            for (u32 i=0; i<c.order().size(); ++i) {
                Layer* layer = c.layer(c.order().at(i));
                if (!layer) continue;
                for (auto& effect : layer->effects) {
                    // Motion Tile na disposição anterior: converte antes de completar.
                    (void)motion_tile::upgrade_legacy_layout(*layer, effect);
                    const auto* specs = effectRegistry_.params(effect.type);
                    if (!specs || effect.params.size() >= specs->count()) continue;
                    EffectInstance defaults;
                    initialize_instance(defaults, *specs);
                    for (u32 p=static_cast<u32>(effect.params.size()); p<specs->count(); ++p) {
                        auto slot = defaults.params[p];
                        if (specs->at(p).type == ParamType::Curve) {
                            effect.curves.push_back(defaults.curves[slot.constant.ref]);
                            slot.constant.ref = effect.curves.size()-1;
                        } else if (specs->at(p).type == ParamType::Gradient) {
                            effect.gradients.push_back(defaults.gradients[slot.constant.ref]);
                            slot.constant.ref = effect.gradients.size()-1;
                        }
                        effect.params.push_back(slot);
                    }
                }
            }
        });
        images_.clear();
        models_.clear();
        hdris_.clear();
    cubeLuts_.clear();
        history_.clear();
        modelRevision_.fetch_add(1, std::memory_order_acq_rel);
        selection_.clear();
        if (Composition* c = current_composition()) {
            adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
            playback_ = PlaybackController{};
            playback_.configure(c->fps(), c->duration());
        }
        // Recuperado de cópia/parcial: está sujo (o principal ainda é o ruim).
        if (mainFileSuspect_) project_->mark_dirty();
    }
    u32 missingTotal = 0;
    // Imagens: o projeto guarda a origem; os pixels voltam pela plataforma. Fora
    // do lock do modelo (decodificar um JPEG grande custa dezenas de ms).
    if (config_.imageLoader) {
        std::vector<std::pair<u64, std::string>> pending;
        {
            std::lock_guard<std::mutex> lock(modelMutex_);
            project_->for_each_asset([&](AssetId id, const Asset& a) {
                if (a.kind == AssetKind::Image && !a.sourcePath.empty()) pending.emplace_back(id.pack(), a.sourcePath);
            });
        }
        u32 missing = 0;
        for (const auto& [key, src] : pending) {
            ImagePixels px;
            const auto resolved=resolve_asset_path(src);
            if (!config_.imageLoader(resolved.c_str(), px, config_.imageLoaderContext) || px.width == 0 || px.height == 0
                || px.rgba.size() != static_cast<usize>(px.width) * px.height * 4) {
                ++missing;
                continue;
            }
            std::lock_guard<std::mutex> lock(modelMutex_);
            images_[key] = std::move(px);
        }
        if (missing) AUREA_LOG_WARN("%u imagem(ns) do projeto nao puderam ser abertas", missing);
        missingTotal += missing;
    }
    // Fontes importadas usadas pelo projeto voltam ao seletor. Ausente = o
    // texto desenha com a fonte padrão (o nome fica guardado para religar).
    {
        std::vector<std::string> fonts;
        {
            std::lock_guard<std::mutex> lock(modelMutex_);
            project_->timeline().for_each_composition([&](CompositionId, const Composition& c) {
                for (u32 i = 0; i < c.order().size(); ++i) {
                    const Layer* l = c.layer(c.order().at(i));
                    if (l && l->kind == LayerKind::Text && !l->text.fontPath.empty()) fonts.push_back(resolve_asset_path(l->text.fontPath));
                }
            });
        }
        std::sort(fonts.begin(), fonts.end());
        fonts.erase(std::unique(fonts.begin(), fonts.end()), fonts.end());
        u32 missing = 0;
        for (const std::string& f : fonts) {
            if (!text::FontManager::instance().add_file(f, true)) ++missing;
        }
        if (missing) AUREA_LOG_WARN("%u fonte(s) do projeto ausente(s): texto na fonte padrao", missing);
        missingTotal += missing;
    }
    // Modelos 3D: reabertos do caminho guardado (relativo ao sandbox). Ausente
    // = o projeto abre mesmo assim; a layer fica sem desenhar e a UI mostra
    // "modelo 3D ausente" para religar.
    {
        std::vector<std::pair<u64, std::string>> pending;
        std::vector<u64> letterAssets;
        {
            std::lock_guard<std::mutex> lock(modelMutex_);
            project_->timeline().for_each_composition([&](CompositionId, const Composition& c) {
                for (u32 i = 0; i < c.order().size(); ++i) {
                    const Layer* l = c.layer(c.order().at(i));
                    if (l && l->kind == LayerKind::Model3D && std::any_of(l->effects.begin(), l->effects.end(), [](const EffectInstance& effect) {
                        return effect.type == effect_type_id(text::kAnimatorEffect) || effect.type == effect_type_id(text::kTransformEffect) || effect.type == effect_type_id(effect_keys::kText3DLayout);
                    })) letterAssets.push_back(l->model.scene.pack());
                }
            });
            project_->for_each_asset([&](AssetId id, const Asset& a) {
                if (a.kind == AssetKind::Model3D && !a.sourcePath.empty()) pending.emplace_back(id.pack(), a.sourcePath);
            });
        }
        u32 missing = 0;
        for (const auto& [key, src] : pending) {
            scene3d::ImportOptions o;
            o.maxTextureSize = model_texture_cap();
            // Texto 3D: a origem é a receita; a malha é gerada de novo.
            scene3d::Text3DSpec spec;
            const auto font = scene3d::decode_text3d(src, spec) ? scene3d::text3d_font(spec) : nullptr;
            bool repairLetters = font && !spec.separateGlyphs && std::find(letterAssets.begin(), letterAssets.end(), key) != letterAssets.end();
            if (repairLetters) spec.separateGlyphs = true;
            // Forma 3D: também é receita (forma + cor/imagem por parte).
            scene3d::Shape3DSpec shape;
            const bool isShape = !font && scene3d::decode_shape3d(src, shape);
            scene3d::ImportResult r = font ? scene3d::build_text3d(*font, spec)
                                    : isShape ? scene3d::build_shape3d(shape, [this](const std::string& s) { return resolve_asset_path(s); },
                                                                       o.maxTextureSize)
                                              : scene3d::import_scene_file(resolve_asset_path(src), o);
            if (!r.ok() && repairLetters) {
                // An older project can exceed the per-letter geometry limit.
                // Preserve its visible text if the repair cannot be built.
                repairLetters = false; spec.separateGlyphs = false;
                r = scene3d::build_text3d(*font, spec);
            }
            if (!r.ok()) {
                ++missing;
                AUREA_LOG_WARN("modelo 3D do projeto nao abriu (%s)", r.detail.c_str());
                continue;
            }
            std::lock_guard<std::mutex> lock(modelMutex_);
            models_[key] = std::shared_ptr<const scene3d::SceneAsset>(std::move(r.asset));
            if (repairLetters) {
                // Older paste/preset paths saved the effect with a merged mesh.
                // Repair the recipe too so the animation survives another save.
                if (Asset* asset = project_->asset(AssetId::unpack(key))) asset->sourcePath = scene3d::encode_text3d(spec);
                project_->mark_dirty();
            }
        }
        if (missing) AUREA_LOG_WARN("%u modelo(s) 3D ausente(s) no projeto", missing);
        missingTotal += missing;
    }
    if (missingTotal) notice |= kLoadMissingMedia;
    lastLoadMissing_.store(missingTotal, std::memory_order_relaxed);
    lastLoadNotice_.store(notice, std::memory_order_relaxed);
    set_last_error(OkStatus, nullptr);
    request_render();
    return OkStatus;
}

Status Engine::save_project(const char* path) noexcept {
    if (!path || !*path) return Errc::InvalidArgument;
    return save_project_impl(path);
}

Status Engine::save_project_impl(const char* requestedPath, bool idleOnly, bool dirtyOnly, bool* saved) noexcept {
    if (saved) *saved = false;
    u64 session;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Errc::InvalidState;
        session = projectSession_;
    }
    // Uma gravação por vez: autosave, "Salvar" e ir para segundo plano podem
    // chegar juntos, cada um na sua thread.
    std::lock_guard<std::mutex> saveLock(saveMutex_);

    std::vector<u8> bytes;
    u64 generation = 0;
    u32 revision = 0;
    bool keepBackup = true;
    SaveOptions options;
    std::string path;
    const u64 t0 = monotonic_ns();
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Errc::InvalidState;
        if (session != projectSession_) return Errc::Cancelled;
        path = requestedPath ? requestedPath : project_->path();
        if (path.empty()) return Status{Errc::InvalidState, "projeto nunca foi salvo"};
        // A UI pode salvar logo depois de submit_commands, antes do próximo
        // frame (inclusive sem GPU ou em segundo plano). O snapshot precisa
        // incluir essa edição. O mesmo lock serializa os consumidores da
        // fila no render e aqui; não há renderização nem espera pela GPU.
        drain_commands_locked();
        if (idleOnly && (!project_->dirty() || playback_.mode() != PlaybackMode::Paused || history_.in_group()))
            return OkStatus;
        if (dirtyOnly && !project_->dirty()) return OkStatus;
        project_->metadata().modifiedUnixMs = wall_clock_ms();
        if (const Status s = ProjectSerializer::encode(*project_, options, bytes); !s.ok()) {
            set_last_error(s, "salvar projeto");
            return s;
        }
        generation = project_->edit_generation();
        revision = modelRevision_.load(std::memory_order_acquire);
        // Principal ruim (aberto da cópia): não vira .bak — o .bak bom fica.
        const bool samePath = project_->path() == path;
        keepBackup = !(mainFileSuspect_ && samePath);
    }
    const u64 t1 = monotonic_ns();

    options.keepBackup = keepBackup;
    std::string error;
    const Status s = ProjectSerializer::write_encoded(bytes, path.c_str(), options, &error);
    const u64 t2 = monotonic_ns();
    {
        std::lock_guard<std::mutex> sl(saveStatsMutex_);
        saveStats_.lastLockNs = t1 - t0;
        saveStats_.lastWriteNs = t2 - t1;
        saveStats_.maxLockNs = std::max(saveStats_.maxLockNs, t1 - t0);
        saveStats_.lastBytes = bytes.size();
        saveStats_.lastError = s.code();
        if (s.ok()) ++saveStats_.saves; else ++saveStats_.failures;
    }
    if (!s.ok()) {
        // O arquivo anterior está intacto (FileIO); o projeto segue sujo e o
        // próximo autosave tenta de novo.
        set_last_error(s, "salvar projeto");
        return s;
    }
    if (saved) *saved = true;

    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_ || session != projectSession_) return OkStatus;
    project_->set_path(path);
    mainFileSuspect_ = false;
    // Só limpa se ninguém mexeu durante a escrita: a edição feita durante o
    // fsync continua "suja" e entra no próximo autosave.
    if (modelRevision_.load(std::memory_order_acquire) == revision) project_->mark_clean_if(generation);
    project_->discard_recovery();
    return OkStatus;
}

Engine::SaveStats Engine::save_stats() const noexcept {
    std::lock_guard<std::mutex> sl(saveStatsMutex_);
    return saveStats_;
}
Status Engine::save_project() noexcept {
    return save_project_impl(nullptr);
}

Status Engine::autosave_project() noexcept {
    return save_project_impl(nullptr, true);
}

Status Engine::save_project_if_dirty(bool* saved) noexcept {
    return save_project_impl(nullptr, false, true, saved);
}

const AutosaveState& Engine::autosave_state() const noexcept {
    static const AutosaveState kEmpty{};
    return project_ ? project_->autosave() : kEmpty;
}

Status Engine::recover_session() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Errc::InvalidState;
    if (const Status s = project_->apply_recovery(); !s.ok()) return s;
    u32 applied = 0, failed = 0;
    for (const Command& cmd : project_->recovered_commands()) {
        const Status s = apply_command_internal(cmd, nullptr, false);
        if (s.ok()) ++applied; else ++failed;
    }
    project_->clear_recovered_commands();
    project_->mark_dirty();
    AUREA_LOG_INFO("recuperacao: %u comandos aplicados, %u ignorados", applied, failed);
    return OkStatus;
}

void Engine::discard_recovery() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (project_) project_->discard_recovery();
}

// =============================================================================
// Importação
// =============================================================================
Result<u64> Engine::import_video(const VideoImport& request) noexcept {
    VideoSourceFactory* factory = config_.mediaFactory;
    if (!factory) return Status{Errc::NotSupported, "sem decodificador de video nesta plataforma"};

    // Sondagem FORA do lock: abrir o container custa alguns ms e a UI e o
    // render não podem esperar por isso.
    MediaProbe probe;
    if (!factory->probe(request.sourcePath.c_str(), probe) || !probe.hasVideo) {
        return Status{Errc::UnsupportedFormat, "arquivo sem trilha de video decodificavel"};
    }
    const VideoStreamInfo& v = probe.video;
    const u32 dispW = v.display_width();
    const u32 dispH = v.display_height();
    if (dispW == 0 || dispH == 0) return Status{Errc::AssetCorrupted, "video sem dimensoes"};

    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};

    history_.before_mutation(*comp, project_->timeline().current(), "importar video");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);

    Asset asset;
    asset.kind = AssetKind::Video;
    asset.name = request.displayName.empty() ? std::string("Video") : request.displayName;
    asset.sourcePath = request.sourcePath;
    asset.originalFilename = request.displayName;
    asset.video.width = dispW;
    asset.video.height = dispH;
    asset.video.fps = v.fps > 0.0 ? v.fps : 30.0;
    asset.timebaseFps = asset.video.fps;
    asset.video.frameCount = FrameIndex{static_cast<i64>(std::llround(
        static_cast<f64>(v.durationUs) * asset.video.fps / 1e6))};
    asset.duration = asset.video.frameCount;
    asset.profile.bitDepth = v.color.bitDepth;
    asset.profile.hdr = v.color.hdr();
    asset.profile.transfer = v.color.transfer == TransferFunction::PQ ? ColorSpace::HDR10
                           : v.color.transfer == TransferFunction::HLG ? ColorSpace::HLG : ColorSpace::Rec709;
    asset.profile.primaries = v.color.primaries == ColorPrimaries::BT2020 ? ColorSpace::Rec2020
                            : v.color.primaries == ColorPrimaries::P3 ? ColorSpace::DisplayP3 : ColorSpace::Rec709;
    if (probe.hasAudio) {
        asset.audio.sampleRate = probe.audioSampleRate;
        asset.audio.channels = probe.audioChannels;
        asset.audio.sampleCount = FrameIndex{probe.audioDurationUs * static_cast<i64>(probe.audioSampleRate) / 1'000'000};
    } else {
        // AudioTrackInfo defaults to stereo for audio creation. A silent video
        // must not schedule mixer/waveform decoders or expose Extract Audio.
        asset.audio.sampleRate = 0;
        asset.audio.channels = 0;
    }
    const AssetId assetId = project_->add_asset(std::move(asset));

    // Importing media must preserve the format explicitly chosen for the project.
    // Fit the layer below; only the duration grows to accommodate the clip.
    const bool first = comp->layers().count() == 0;
    const f64 fps = comp->fps();
    const i64 frames = std::max<i64>(1, static_cast<i64>(std::ceil(static_cast<f64>(v.durationUs) * fps / 1e6 - 1e-6)));
    if (first || frames > comp->duration().value) comp->set_duration(FrameIndex{frames});

    const LayerId lid = comp->add_layer(LayerKind::Video, request.displayName);
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->source = assetId;
    l->start = FrameIndex{0};
    l->end = FrameIndex{frames};
    l->transform.anchor = Vec3{static_cast<f32>(dispW) * 0.5f, static_cast<f32>(dispH) * 0.5f, 0.0f};
    l->transform.position = Vec3{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    const f32 fit = std::min(static_cast<f32>(comp->width()) / static_cast<f32>(dispW),
                             static_cast<f32>(comp->height()) / static_cast<f32>(dispH));
    l->transform.scale = Vec3{fit, fit, 1.0f};

    adapt().configure(comp->width(), comp->height(), static_cast<f32>(comp->fps()));
    playback_.configure(comp->fps(), comp->duration());
    if (first) playback_.seek(FrameIndex{0}, monotonic_ns());
    project_->mark_dirty();
    request_render();
    AUREA_LOG_INFO("video importado: %ux%u %.3f fps, %lld us, cor %s", dispW, dispH, v.fps,
                   static_cast<long long>(v.durationUs), v.color.fromStream ? "do arquivo" : "deduzida");
    return lid.pack();
}

Result<u64> Engine::import_audio(const VideoImport& request) noexcept {
    VideoSourceFactory* factory = config_.mediaFactory;
    if (!factory) return Status{Errc::NotSupported, "sem decodificador de audio nesta plataforma"};
    MediaProbe probe;
    if (!factory->probe(request.sourcePath.c_str(), probe) || !probe.hasAudio || probe.audioSampleRate == 0) {
        return Status{Errc::UnsupportedFormat, "arquivo sem trilha de audio decodificavel"};
    }
    if (probe.audioDurationUs <= 0) return Status{Errc::AssetCorrupted, "audio sem duracao"};

    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    history_.before_mutation(*comp, project_->timeline().current(), "importar audio");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);

    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    Asset asset;
    asset.kind = AssetKind::Audio;
    asset.name = request.displayName.empty() ? std::string("Audio") : request.displayName;
    asset.sourcePath = request.sourcePath;
    asset.originalFilename = request.displayName;
    asset.audio.sampleRate = probe.audioSampleRate;
    asset.audio.channels = probe.audioChannels;
    asset.audio.sampleCount = FrameIndex{probe.audioDurationUs * static_cast<i64>(probe.audioSampleRate) / 1'000'000};
    asset.timebaseFps = fps;
    const i64 frames = std::max<i64>(1, static_cast<i64>(std::ceil(static_cast<f64>(probe.audioDurationUs) * fps / 1e6 - 1e-6)));
    asset.duration = FrameIndex{frames};
    const AssetId assetId = project_->add_asset(std::move(asset));

    const bool first = comp->layers().count() == 0;
    if (first || frames > comp->duration().value) comp->set_duration(FrameIndex{frames});
    const LayerId lid = comp->add_layer(LayerKind::Audio, request.displayName);
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->source = assetId;
    l->start = FrameIndex{0};
    l->end = FrameIndex{frames};
    playback_.configure(comp->fps(), comp->duration());
    project_->mark_dirty();
    request_render();
    AUREA_LOG_INFO("audio importado: %u Hz, %u canais, %lld us", probe.audioSampleRate, probe.audioChannels,
                   static_cast<long long>(probe.audioDurationUs));
    return lid.pack();
}

Result<u64> Engine::extract_audio(u64 videoLayerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    Layer* v = comp->layer(LayerId::unpack(videoLayerId));
    if (!v || v->kind != LayerKind::Video) return Status{Errc::NotFound, "camada de video nao encontrada"};
    const Asset* a = project_->asset(v->source);
    if (!a || !a->has_audio()) return Status{Errc::NotSupported, "este video nao tem som"};
    history_.before_mutation(*comp, project_->timeline().current(), "extrair audio");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);

    // Copia o que é de som (tempo, corte, ganho, fades, volume animado); o
    // vídeo fica mudo — sem isso o som tocaria dobrado.
    const Layer src = *v;
    const LayerId lid = comp->add_layer(LayerKind::Audio, src.name.empty() ? std::string("Audio") : src.name + " (audio)");
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->source = src.source;
    l->start = src.start;
    l->end = src.end;
    l->offset = src.offset;
    l->gain = src.gain;
    l->pan = src.pan;
    l->fadeIn = src.fadeIn;
    l->fadeOut = src.fadeOut;
    l->solo = src.solo;
    if (const Track* vt = src.tracks.find(TrackProperty::AudioVolume)) {
        l->tracks.get_or_create(TrackProperty::AudioVolume) = *vt;
    }
    if (Layer* vv = comp->layer(LayerId::unpack(videoLayerId))) vv->muted = true;
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

Result<u64> Engine::freeze_frame(u64 layerId, i64 frame, i64 holdFrames) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    const LayerId id = LayerId::unpack(layerId);
    Layer* l = comp->layer(id);
    if (!l || l->kind != LayerKind::Video) return Status{Errc::NotFound, "camada de video nao encontrada"};
    if (frame < l->start.value || frame >= l->end.value) return Status{Errc::OutOfRange, "o cabecote nao esta sobre o clipe"};
    holdFrames = std::max<i64>(1, holdFrames);
    history_.before_mutation(*comp, project_->timeline().current(), "congelar quadro");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);

    // Quadro exato da fonte no cabeçote (antes de mexer em qualquer coisa).
    const i64 srcFrame = static_cast<i64>(std::floor(l->source_frame(FrameIndex{frame}) + 1e-6));
    // Copiados antes da divisão: criar camada pode realocar a tabela (e `l`).
    const AssetId srcAsset = l->source;
    const std::string srcName = l->name;
    // 1) O resto do clipe (depois do cabeçote) anda `holdFrames` para frente.
    if (frame > l->start.value) {
        Command split;
        split.type = CommandType::LayerSplit;
        split.layer_split.layer = id;
        split.layer_split.at = FrameIndex{frame};
        if (const Status s = apply_command_internal(split, nullptr, false); !s.ok()) return s;
    }
    // A metade de depois é a camada criada pela divisão (ou a própria, se o
    // cabeçote estava no primeiro quadro).
    LayerId after = id;
    comp->layers().for_each([&](LayerId lid, const Layer& o) {
        if (lid != id && o.source == srcAsset && o.start.value == frame && o.kind == LayerKind::Video) after = lid;
    });
    if (Layer* a = comp->layer(after)) {
        a->start = FrameIndex{a->start.value + holdFrames};
        a->end = FrameIndex{a->end.value + holdFrames};
    }
    // 2) O quadro parado, no lugar.
    const LayerId hold = comp->duplicate_layer(id, FrameIndex{frame});
    Layer* h = comp->layer(hold);
    if (!h) return Status{Errc::OutOfMemory, "camada nao criada"};
    h->name = (srcName.empty() ? std::string("Video") : srcName) + " (congelado)";
    h->start = FrameIndex{frame};
    h->end = FrameIndex{frame + holdFrames};
    h->offset = FrameIndex{srcFrame};
    h->speed = 0.0f;
    h->reversed = false;
    i64 maxEnd = comp->duration().value;
    comp->layers().for_each([&](LayerId, const Layer& o) { maxEnd = std::max(maxEnd, o.end.value); });
    if (maxEnd > comp->duration().value) comp->set_duration(FrameIndex{maxEnd});
    playback_.configure(comp->fps(), comp->duration());
    project_->mark_dirty();
    request_render();
    return hold.pack();
}

void Engine::recenter_text(Layer& l) noexcept {
    if (l.kind != LayerKind::Text) return;
    const auto font = text::FontManager::instance().font_for(l.text);
    if (!font) return;
    // A caixa cresce/encolhe em volta do centro: o texto não "anda" ao editar.
    const text::TextExtent ext = text::measure(*font, l.text);
    const f32 pad = l.text.strokeWidth > 0.0f ? l.text.strokeWidth + 2.0f : 2.0f;
    const Vec3 oldAnchor = l.transform.anchor;
    const Vec3 anchor{std::ceil(ext.width + 2.0f * pad) * 0.5f, std::ceil(ext.height + 2.0f * pad) * 0.5f, 0.0f};
    l.transform.anchor = anchor;
    if (Track* ax = l.tracks.find(TrackProperty::AnchorX); ax && ax->keys.empty()) ax->staticValue = anchor.x;
    if (Track* ay = l.tracks.find(TrackProperty::AnchorY); ay && ay->keys.empty()) ay->staticValue = anchor.y;
    (void)oldAnchor;
}

bool Engine::query_text(u64 layerId, TextData& out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = current_composition();
    if (!comp) return false;
    const Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l || l->kind != LayerKind::Text) return false;
    out = l->text;
    return true;
}

Result<u64> Engine::add_text(const char* content) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    if (!text::default_font()) return Status{Errc::NotSupported, "nenhuma fonte disponivel neste aparelho"};
    history_.before_mutation(*comp, project_->timeline().current(), "adicionar texto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const LayerId lid = comp->add_layer(LayerKind::Text, "Texto");
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->text.content = content && *content ? content : "Texto";
    l->text.size = std::round(0.12f * static_cast<f32>(std::min(comp->width(), comp->height())));
    l->text.color = Vec4{1, 1, 1, 1};
    l->text.alignment = 1;
    // O cursor pode estar DEPOIS do fim da composição (a timeline não trava
    // mais no fim): a camada nasce onde ele está e a duração acompanha, senão
    // ela nasceria fora do projeto e não apareceria.
    const i64 t = std::max<i64>(0, playback_.current().value);
    l->start = FrameIndex{t};
    l->end = FrameIndex{std::max<i64>(t + 1, comp->duration().value)};
    if (l->end.value > comp->duration().value) {
        comp->set_duration(l->end);
        playback_.configure(comp->fps(), comp->duration());
    }
    l->transform.position = Vec3{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    recenter_text(*l);
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

namespace {

/// Inversa geral 4×4 (cofatores). Matrizes de camada são afins e inversíveis
/// (escala 0 devolve identidade — nada a compensar).
Mat4 inverse4(const Mat4& a) noexcept {
    const f32* m = &a.col[0].x;
    f32 inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    const f32 det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return Mat4::identity();
    Mat4 r;
    f32* o = &r.col[0].x;
    for (int i = 0; i < 16; ++i) o[i] = inv[i] / det;
    return r;
}

/// Escreve o transform local (posição/rotação/escala, âncora mantida) que
/// reproduz `local` (camada → espaço do pai). Trilhas sem keyframe acompanham.
/// Usado pelo parentesco compensado e ao tirar a camada de perto do pai.
void set_local_from(Layer& lay, const Mat4& local, const Vec3* sampledAnchor = nullptr) noexcept {
    Layer* l = &lay;
    const Vec3 anc = sampledAnchor ? *sampledAnchor : l->transform.anchor;
    const Mat4 m = local * Mat4::translation(anc);
    const Vec3 c0{m.col[0].x, m.col[0].y, m.col[0].z};
    const Vec3 c1{m.col[1].x, m.col[1].y, m.col[1].z};
    const Vec3 c2{m.col[2].x, m.col[2].y, m.col[2].z};
    const f32 sx = c0.length() * (c0.dot(c1.cross(c2)) < 0.f ? -1.f : 1.f);
    const f32 sy = c1.length(), sz = std::max(1e-6f, c2.length());
    if (!(std::fabs(sx) > 1e-6f && sy > 1e-6f)) return;
    const f32 r20 = c0.z / sx, r21 = c1.z / sy, r22 = c2.z / sz, r10 = c0.y / sx, r00 = c0.x / sx;
    const f32 ry = std::asin(std::clamp(-r20, -1.0f, 1.0f));
    const f32 rx = std::atan2(r21, r22);
    const f32 rz = std::atan2(r10, r00);
    Vec3 rotDeg{rx / kDeg2Rad, ry / kDeg2Rad, rz / kDeg2Rad};
    // Camada 2D fica 2D: sem inclinar em X/Y por arredondamento.
    if (std::fabs(rotDeg.x) < 1e-3f) rotDeg.x = 0.0f;
    if (std::fabs(rotDeg.y) < 1e-3f) rotDeg.y = 0.0f;
    const Vec3 pos{m.col[3].x, m.col[3].y, std::fabs(m.col[3].z) < 1e-3f ? 0.0f : m.col[3].z};
    l->transform.position = pos;
    l->transform.rotation = rotDeg;
    // Z guardado relativo a X (o render multiplica), menos câmera e luz.
    l->transform.scale = Vec3{sx, sy, (l->kind == LayerKind::Camera || l->kind == LayerKind::Light) ? sz : sz / sx};
    auto set = [&](TrackProperty prop, f32 v) {
        if (Track* tr = l->tracks.find(prop); tr && tr->keys.size() <= 1) {
            if (tr->keys.size() == 1) tr->keys[0].value = v; else tr->staticValue = v;
        }
    };
    set(TrackProperty::PositionX, pos.x); set(TrackProperty::PositionY, pos.y); set(TrackProperty::PositionZ, pos.z);
    set(TrackProperty::RotationX, rotDeg.x); set(TrackProperty::RotationY, rotDeg.y); set(TrackProperty::RotationZ, rotDeg.z);
    set(TrackProperty::ScaleX, sx); set(TrackProperty::ScaleY, sy);
    set(TrackProperty::ScaleZ, l->transform.scale.z);
}

/// Transform parado (posição/rotação/escala sem keyframes)? Só aí dá para
/// reescrever o local sem destruir uma animação.
bool transform_is_static(const Layer& l) noexcept {
    for (TrackProperty p : {TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ,
                            TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ,
                            TrackProperty::ScaleX, TrackProperty::ScaleY}) {
        if (const Track* t = l.tracks.find(p); t && t->animated()) return false;
    }
    return true;
}

} // namespace

u32 Engine::query_materials(u64 layerId, f32* values, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->kind != LayerKind::Model3D) return 0;
    const auto found = models_.find(layer->model.scene.pack());
    if (found == models_.end() || !found->second) return 0;
    const auto& materials = found->second->materials;
    if (!values) return static_cast<u32>(materials.size());
    const auto local = layer->local_time(playback_.current());
    const u32 count = std::min<u32>(capacity, static_cast<u32>(materials.size()));
    for (u32 i = 0; i < count; ++i) {
        const auto& source = materials[i];
        f32 fields[6]{source.baseColor.x, source.baseColor.y, source.baseColor.z, source.baseColor.w, source.metallic, source.roughness};
        u32 mask = 0;
        for (const auto& over : layer->model.materials) if (over.materialIndex == i) {
            mask = over.mask;
            const f32 overrides[6]{over.baseColor.x, over.baseColor.y, over.baseColor.z, over.baseColor.w, over.metallic, over.roughness};
            for (u32 p = 0; p < 6; ++p) if (mask & (1u << p)) fields[p] = overrides[p];
            break;
        }
        for (u32 p = 0; p < 6; ++p) if (const Track* track = layer->tracks.find(TrackProperty::MaterialParam, i, p)) {
            fields[p] = track->value_or(local, fields[p]); mask |= 1u << p;
        }
        values[i * 8] = static_cast<f32>(i); values[i * 8 + 1] = static_cast<f32>(mask);
        for (u32 p = 0; p < 6; ++p) values[i * 8 + 2 + p] = std::clamp(fields[p], 0.0f, 1.0f);
    }
    return count;
}

Status Engine::set_material_param(u64 layer, u32 material, u32 param, f32 value) noexcept {
    Command c; c.type = CommandType::LayerSetMaterialParam;
    c.material_param = MaterialParamPayload{LayerId::unpack(layer), material, param, value};
    return apply_command(c);
}

Result<u64> Engine::add_light(u32 kind) noexcept {
    if (kind > 3) return Status{Errc::InvalidArgument};
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState};
    const bool flare = kind == 3;
    const auto* flareParams = flare ? effectRegistry_.params(effect_type_id(effect_keys::kSceneFlare)) : nullptr;
    if (flare && !flareParams) return Status{Errc::NotSupported};
    history_.before_mutation(*comp, project_->timeline().current(), "adicionar luz 3D");
    const LayerId id = comp->add_layer(LayerKind::Light, flare ? "Flare 3D" : kind == 0 ? "Directional light" : kind == 1 ? "Point light" : "Spot light");
    Layer* layer = comp->layer(id);
    if (!layer) return Status{Errc::OutOfMemory};
    layer->threeD = true; layer->end = comp->duration();
    layer->transform.position = Vec3{comp->width() * .5f, comp->height() * .25f, -static_cast<f32>(comp->height())};
    layer->light.kind = static_cast<LightKind>(flare ? 1 : kind);
    layer->light.intensity = kind == 0 ? 3.0f : 8.0f;
    layer->light.range = comp->height() * 5.0f;
    layer->light.castShadows = kind == 0;
    if (flare) {
        layer->transform.position = Vec3{comp->width() * .5f, comp->height() * .5f, 0};
        layer->blendMode = BlendMode::Add;
        EffectInstance effect; effect.id = layer->alloc_effect_id();
        effect.type = effect_type_id(effect_keys::kSceneFlare);
        initialize_instance(effect, *flareParams);
        layer->effects.push_back(std::move(effect));
    }
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty(); request_render(); return id.pack();
}

bool Engine::query_light(u64 layerId, f32* values, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->kind != LayerKind::Light || !values || capacity < 10) return false;
    const auto local = layer->local_time(playback_.current());
    const auto& light = layer->light;
    values[0] = static_cast<f32>(light.kind);
    values[1] = layer->tracks.sample_or(TrackProperty::LightIntensity, local, light.intensity);
    values[2] = layer->tracks.sample_or(TrackProperty::LightColorR, local, light.color.x);
    values[3] = layer->tracks.sample_or(TrackProperty::LightColorG, local, light.color.y);
    values[4] = layer->tracks.sample_or(TrackProperty::LightColorB, local, light.color.z);
    values[5] = light.range;
    values[6] = layer->tracks.sample_or(TrackProperty::LightConeAngle, local, light.coneAngle);
    values[7] = layer->tracks.sample_or(TrackProperty::LightPenumbra, local, light.penumbra);
    values[8] = light.castShadows ? 1.0f : 0.0f; values[9] = light.shadowBias;
    if (capacity >= 11) values[10] = light.shadowStrength;
    return true;
}

bool Engine::query_camera_lens(u64 layerId, f32* values) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->kind != LayerKind::Camera || !values) return false;
    const FrameIndex local = layer->local_time(playback_.current());
    const CameraData& cam = layer->camera;
    // A MESMA conta da projeção: mm com keyframe manda; senão o FOV (parado
    // ou animado) vira o mm equivalente.
    const f32 fov = camera_fov_deg_at(*layer, static_cast<f64>(local.value));
    const Track* focal = layer->tracks.find(TrackProperty::FocalLength);
    values[0] = focal && focal->driven() ? std::clamp(focal->value_or(local, cam.focalLength), kCameraFocalMinMm, kCameraFocalMaxMm)
                                         : camera_focal_from_fov(fov);
    values[1] = fov;
    values[2] = cam.dofEnabled ? 1.0f : 0.0f;
    values[3] = layer->tracks.sample_or(TrackProperty::FocusDistance, local, cam.focusDistance);
    values[4] = layer->tracks.sample_or(TrackProperty::Aperture, local, cam.aperture);
    values[5] = layer->tracks.sample_or(TrackProperty::CameraBlur, local, cam.blurAmount);
    values[6] = scene3d::scene_pixels_per_meter(comp->height());
    u32 mask = 0;
    const TrackProperty props[4]{TrackProperty::FocalLength, TrackProperty::FocusDistance, TrackProperty::Aperture, TrackProperty::CameraBlur};
    for (u32 i = 0; i < 4; ++i) if (const Track* t = layer->tracks.find(props[i]); t && !t->keys.empty()) mask |= 1u << i;
    values[7] = static_cast<f32>(mask);
    values[8] = cam.active ? 1.0f : 0.0f;
    return true;
}

f32 Engine::pick_focus_distance(u64 layerId, f32 compX, f32 compY) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->kind != LayerKind::Camera) return -1.0f;
    const FrameIndex now = playback_.current();
    Vec3 hit{};
    if (!pick_scene_point(*comp, now, compX, compY, &Engine::model_lookup, this, hit)) return -1.0f;
    // Plano de foco perpendicular ao eixo ótico DESTA câmera (a mesma
    // profundidade que o DOF lê do Z da cena).
    const Mat4 wm = layer_world_3d(*comp, *layer, now);
    Vec3 forward = Vec3{wm.col[2].x, wm.col[2].y, wm.col[2].z}.normalized();
    if (forward.length_sq() < kEpsilon) forward = Vec3{0, 0, 1};
    const f32 distance = (hit - Vec3{wm.col[3].x, wm.col[3].y, wm.col[3].z}).dot(forward);
    return std::isfinite(distance) && distance > 0.01f ? distance : -1.0f;
}

Result<u64> Engine::add_camera() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    history_.before_mutation(*comp, project_->timeline().current(), "adicionar camera 3D");
    const LayerId id = comp->add_layer(LayerKind::Camera, "Camera 3D");
    Layer* camera = comp->layer(id);
    if (!camera) return Status{Errc::OutOfMemory, "camada nao criada"};
    const auto fallback = scene3d::default_camera(comp->width(), comp->height());
    camera->threeD = true;
    camera->start = FrameIndex{0};
    camera->end = comp->duration();
    camera->transform.position = fallback.position;
    camera->transform.anchor = Vec3{0, 0, 0};
    camera->camera.fov = fallback.fovY / kDeg2Rad;
    // Lente sincronizada com a projeção; o foco nasce no plano da composição.
    camera->camera.focalLength = camera_focal_from_fov(camera->camera.fov);
    camera->camera.focusDistance = std::max(1.0f, -fallback.position.z);
    camera->camera.nearPlane = fallback.nearZ;
    camera->camera.active = true;
    for (u32 i = 0; i < comp->order().size(); ++i) {
        Layer* other = comp->layer(comp->order().at(i));
        if (other && other != camera && other->kind == LayerKind::Camera) other->camera.active = false;
    }
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return id.pack();
}

namespace {
/// "Nulo", "Nulo 2", "Nulo 3"…: o menor número livre entre as camadas da
/// composição. Nulos com o MESMO nome eram indistinguíveis na timeline, no
/// vínculo (pai) e nas fichas da cena — parecia que só os primeiros existiam.
std::string numbered_layer_name(const Composition& comp, const std::string& base) {
    std::vector<u32> used;
    for (u32 i = 0; i < comp.order().size(); ++i) {
        const Layer* l = comp.layer(comp.order().at(i));
        if (!l) continue;
        if (l->name == base) { used.push_back(1); continue; }
        if (l->name.size() <= base.size() + 1 || l->name.compare(0, base.size(), base) != 0 || l->name[base.size()] != ' ') continue;
        const std::string tail = l->name.substr(base.size() + 1);
        if (tail.empty() || tail.size() > 6 || !std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        used.push_back(static_cast<u32>(std::stoul(tail)));
    }
    for (u32 n = 1;; ++n) {
        if (std::find(used.begin(), used.end(), n) == used.end()) return n == 1 ? base : base + " " + std::to_string(n);
    }
}
} // namespace

Result<u64> Engine::add_null(bool threeD) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    // A fila vem ANTES (como em parent_to_new_null): um desfazer ainda na fila
    // rodaria DEPOIS deste passo síncrono e apagaria o nulo recém-criado.
    drain_commands_locked();
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    history_.before_mutation(*comp, project_->timeline().current(), threeD ? "adicionar nulo 3D" : "adicionar nulo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const LayerId lid = comp->add_layer(LayerKind::Null, numbered_layer_name(*comp, threeD ? "Nulo 3D" : "Nulo"));
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->threeD = threeD;
    // O cursor pode estar DEPOIS do fim da composição (a timeline não trava
    // mais no fim): a camada nasce onde ele está e a duração acompanha, senão
    // ela nasceria fora do projeto e não apareceria.
    const i64 t = std::max<i64>(0, playback_.current().value);
    l->start = FrameIndex{t};
    l->end = FrameIndex{std::max<i64>(t + 1, comp->duration().value)};
    if (l->end.value > comp->duration().value) {
        comp->set_duration(l->end);
        playback_.configure(comp->fps(), comp->duration());
    }
    l->transform.anchor = Vec3{50.0f, 50.0f, 0.0f};   // caixa virtual de 100 px (alças no palco)
    // No centro — e, se outro nulo já está parado ali, um degrau na diagonal:
    // nulos empilhados no mesmo ponto só deixavam tocar o de cima no palco.
    const Vec3 center{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    const f32 step = std::max(8.0f, static_cast<f32>(std::min(comp->width(), comp->height())) * 0.05f);
    auto occupied = [&](const Vec3& p) {
        for (u32 i = 0; i < comp->order().size(); ++i) {
            const Layer* other = comp->layer(comp->order().at(i));
            if (!other || other == l || other->kind != LayerKind::Null || other->parent.valid()) continue;
            if (other->tracks.find(TrackProperty::PositionX) || other->tracks.find(TrackProperty::PositionY)) continue;
            if (std::fabs(other->transform.position.x - p.x) < 0.5f && std::fabs(other->transform.position.y - p.y) < 0.5f) return true;
        }
        return false;
    };
    Vec3 at = center;
    for (u32 k = 1; k < 64 && occupied(at); ++k) at = Vec3{center.x + step * static_cast<f32>(k), center.y + step * static_cast<f32>(k), 0.0f};
    l->transform.position = at;
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

Result<u64> Engine::parent_to_new_null(const u64* layerIds, u32 count) noexcept {
    if (!layerIds || count == 0 || count > 4096) return Status{Errc::InvalidArgument, "nenhuma camada"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    // Edições ainda na fila (arrastos, keys) vêm ANTES: a compensação mede o estado final.
    drain_commands_locked();
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    const FrameIndex now = playback_.current();
    std::vector<LayerId> children;
    bool threeD = false;
    Vec3 center{0.0f, 0.0f, 0.0f};
    i64 start = std::numeric_limits<i64>::max(), end = 0;
    for (u32 i = 0; i < count; ++i) {
        const LayerId id = LayerId::unpack(layerIds[i]);
        const Layer* l = comp->layer(id);
        if (!l || l->locked || std::find(children.begin(), children.end(), id) != children.end()) continue;
        const bool in3d = l->kind == LayerKind::Model3D || l->kind == LayerKind::Camera || l->kind == LayerKind::Light
                       || l->threeD || wants_layer_3d(*comp, *l, now);
        threeD = threeD || in3d;
        // Ponto de ancoragem no mundo = onde a camada "está" na tela.
        const Mat4 w = in3d ? layer_world_3d(*comp, *l, now) : layer_world_matrix(*comp, *l, now);
        const Vec4 o = w * Vec4{l->transform.anchor.x, l->transform.anchor.y, l->transform.anchor.z, 1.0f};
        const f32 inv = std::fabs(o.w) > 1e-6f ? 1.0f / o.w : 1.0f;
        if (!std::isfinite(o.x * inv) || !std::isfinite(o.y * inv) || !std::isfinite(o.z * inv)) continue;
        center = Vec3{center.x + o.x * inv, center.y + o.y * inv, center.z + (in3d ? o.z * inv : 0.0f)};
        start = std::min(start, l->start.value);
        end = std::max(end, l->end.value);
        children.push_back(id);
    }
    if (children.empty()) return Status{Errc::InvalidArgument, "nenhuma camada desbloqueada"};
    const f32 n = static_cast<f32>(children.size());
    center = Vec3{center.x / n, center.y / n, center.z / n};

    history_.begin_group("vincular a novo nulo");
    history_.before_mutation(*comp, project_->timeline().current(), "vincular a novo nulo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const LayerId nullId = comp->add_layer(LayerKind::Null, numbered_layer_name(*comp, threeD ? "Nulo 3D" : "Nulo"));
    Layer* nl = comp->layer(nullId);
    if (!nl) {
        history_.end_group();
        return Status{Errc::OutOfMemory, "camada nao criada"};
    }
    nl->threeD = threeD;
    // Vive enquanto os filhos vivem (não some antes deles na timeline).
    nl->start = FrameIndex{std::max<i64>(0, start)};
    nl->end = FrameIndex{std::max<i64>(nl->start.value + 1, end)};
    nl->transform.anchor = Vec3{50.0f, 50.0f, 0.0f};   // caixa virtual de 100 px (alças no palco)
    nl->transform.position = center;
    u32 linked = 0;
    for (const LayerId child : children) {
        Command c;
        c.type = CommandType::LayerSetParent;
        c.layer_parent.layer = child;
        c.layer_parent.parent = nullId;
        if (apply_command_internal(c, nullptr, true).ok()) ++linked;
    }
    history_.end_group();
    project_->mark_dirty();
    request_render();
    AUREA_LOG_INFO("novo nulo %s: %u de %zu camadas vinculadas", threeD ? "3D" : "2D", linked, children.size());
    return nullId.pack();
}

namespace {
/// Toda a animação da camada anda `delta` quadros no tempo LOCAL: trilhas
/// (transform, efeitos, texto, partículas...), caminhos de máscara e formas
/// vetoriais. O time remap fica — ele é o relógio do conteúdo, não animação.
void shift_layer_animation(Layer& l, i64 delta) noexcept {
    for (u32 i = 0; i < l.tracks.size(); ++i) {
        Track& t = l.tracks.at(i);
        for (Keyframe& k : t.keys) k.time.value += delta;
        t.lastIndex = 0;
    }
    for (Mask& m : l.masks) for (MaskPathKey& k : m.pathKeys) k.frame += delta;
    for (VectorGroup& g : l.shape.vector.groups)
        for (VectorPath& p : g.paths) for (PathKey& k : p.keys) k.frame += delta;
}
} // namespace

Result<u32> Engine::stagger_layers(const u64* layerIds, u32 count, i64 stepFrames, bool keysOnly) noexcept {
    if (!layerIds || count < 2 || count > 4096) return Status{Errc::InvalidArgument, "escolha duas camadas ou mais"};
    if (stepFrames == 0 || stepFrames < -100000 || stepFrames > 100000) return Status{Errc::InvalidArgument, "passo invalido"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    // O que a UI enfileirou (arrastos, keys) vale antes: a cascata parte do estado final.
    drain_commands_locked();
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    std::vector<LayerId> order;
    for (u32 i = 0; i < count; ++i) {
        const LayerId id = LayerId::unpack(layerIds[i]);
        const Layer* l = comp->layer(id);
        if (!l || l->locked || std::find(order.begin(), order.end(), id) != order.end()) continue;
        order.push_back(id);
    }
    if (order.size() < 2) return Status{Errc::InvalidArgument, "escolha duas camadas desbloqueadas"};
    if (!keysOnly) {
        // Passo negativo não empurra ninguém para antes do zero (a camada sumiria do começo).
        for (u32 i = 1; i < order.size(); ++i) {
            if (comp->layer(order[i])->start.value + static_cast<i64>(i) * stepFrames < 0)
                return Status{Errc::OutOfRange, "a cascata passaria do inicio do projeto"};
        }
    }
    history_.begin_group("escalonar");
    history_.before_mutation(*comp, project_->timeline().current(), "escalonar");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    u32 moved = 0;
    for (u32 i = 1; i < order.size(); ++i) {
        const i64 delta = static_cast<i64>(i) * stepFrames;
        Layer* l = comp->layer(order[i]);
        if (!l) continue;
        if (keysOnly) {
            shift_layer_animation(*l, delta);
            ++moved;
            continue;
        }
        // Pelo comando de sempre: duração do projeto cresce junto, modo ímã respeitado.
        Command c;
        c.type = CommandType::LayerSetTimeRange;
        c.layer_range.layer = order[i];
        c.layer_range.start = FrameIndex{l->start.value + delta};
        c.layer_range.end = FrameIndex{l->end.value + delta};
        if (apply_command_internal(c, nullptr, true).ok()) ++moved;
    }
    history_.end_group();
    project_->mark_dirty();
    request_render();
    return moved;
}

Result<u32> Engine::arrange_layer_times(const u64* layerIds, u32 count,
                                      LayerTimeArrangement mode, i64 playhead) noexcept {
    constexpr i64 limit = i64{1} << 31;
    if (!layerIds || count == 0 || count > 4096 || static_cast<u32>(mode) > 6)
        return Status{Errc::InvalidArgument, "arranjo de tempo invalido"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhuma composicao"};
    struct Item { LayerId id; i64 start, length, target; };
    std::vector<Item> items;
    for (u32 i = 0; i < count; ++i) {
        const LayerId id = LayerId::unpack(layerIds[i]);
        const Layer* layer = comp->layer(id);
        if (!layer || layer->locked || std::any_of(items.begin(), items.end(),
                [&](const Item& item) { return item.id == id; })) continue;
        if (layer->start.value < 0 || layer->end.value <= layer->start.value || layer->end.value >= limit)
            return Status{Errc::OutOfRange, "intervalo de camada invalido"};
        items.push_back({id, layer->start.value, layer->end.value - layer->start.value, layer->start.value});
    }
    const bool atPlayhead = mode == LayerTimeArrangement::StartsAtPlayhead || mode == LayerTimeArrangement::EndsAtPlayhead;
    const bool distribution = mode == LayerTimeArrangement::DistributeStarts || mode == LayerTimeArrangement::DistributeGaps;
    const usize minimum = atPlayhead ? 1 : distribution ? 3 : 2;
    if (items.size() < minimum) return Status{Errc::InvalidArgument, "camadas desbloqueadas insuficientes"};
    if (atPlayhead && (playhead < 0 || playhead >= limit)) return Status{Errc::OutOfRange, "cabecote fora do intervalo"};
    i64 firstStart = limit, lastEnd = 0;
    for (const Item& item : items) {
        firstStart = std::min(firstStart, item.start);
        lastEnd = std::max(lastEnd, item.start + item.length);
    }
    if (distribution) {
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.start < b.start; });
        const i64 intervals = static_cast<i64>(items.size() - 1);
        if (mode == LayerTimeArrangement::DistributeStarts) {
            const i64 span = items.back().start - items.front().start;
            for (usize i = 0; i < items.size(); ++i)
                items[i].target = firstStart + (span * static_cast<i64>(i) + intervals / 2) / intervals;
        } else {
            // Anchor the earliest and latest starts. Integer rounding is
            // cumulative so there is no drift; neighbouring gaps differ by <=1f.
            i64 occupied = 0;
            for (usize i = 0; i + 1 < items.size(); ++i) occupied += items[i].length;
            const i64 free = items.back().start - firstStart - occupied;
            if (free < 0) return Status{Errc::OutOfRange, "sem espaco para intervalos iguais"};
            i64 elapsed = 0;
            for (usize i = 0; i < items.size(); ++i) {
                items[i].target = firstStart + elapsed + (free * static_cast<i64>(i) + intervals / 2) / intervals;
                elapsed += items[i].length;
            }
        }
    } else {
        i64 cursor = items.front().start;
        for (Item& item : items) {
            switch (mode) {
                case LayerTimeArrangement::AlignStarts: item.target = firstStart; break;
                case LayerTimeArrangement::AlignEnds: item.target = lastEnd - item.length; break;
                case LayerTimeArrangement::Sequence: item.target = cursor; cursor += item.length; break;
                case LayerTimeArrangement::StartsAtPlayhead: item.target = playhead; break;
                case LayerTimeArrangement::EndsAtPlayhead: item.target = playhead - item.length; break;
                default: break;
            }
        }
    }
    u32 moved = 0;
    i64 duration = comp->duration().value;
    for (const Item& item : items) {
        if (item.target < 0 || item.target + item.length >= limit)
            return Status{Errc::OutOfRange, "arranjo fora dos limites da timeline"};
        moved += item.target != item.start;
        duration = std::max(duration, item.target + item.length);
    }
    if (!moved) return u32{0};
    history_.before_mutation(*comp, project_->timeline().current(), "organizar camadas no tempo");
    for (const Item& item : items) {
        Layer* layer = comp->layer(item.id);
        layer->start = FrameIndex{item.target};
        layer->end = FrameIndex{item.target + item.length};
    }
    comp->touch();
    if (duration > comp->duration().value) {
        comp->set_duration(FrameIndex{duration});
        playback_.configure(comp->fps(), comp->duration());
    }
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return moved;
}

namespace {

/// Segue `pts` (px da camada, no 1º quadro de `targetUs`) quadro a quadro por
/// NCC numa miniatura de até 360 px de altura. `out[i][k]` = ponto k no quadro
/// i; ponto perdido (NCC < 0,6) vira NaN e não volta. Para quando sobram menos
/// de `minAlive` pontos. false = vídeo ilegível.
bool follow_points(VideoSourceFactory& factory, const Asset& asset, const std::vector<i64>& targetUs, f64 fps, u32 layerW,
                   u32 layerH, const std::vector<Vec2>& pts, u32 minAlive, std::vector<std::vector<Vec2>>& out) {
    out.clear();
    auto dec = factory.open_video(asset, MediaPriority::Thumbnail);
    if (!dec) return false;
    const u32 thumbH = std::min<u32>(360u, layerH);
    const i64 halfFrame = static_cast<i64>(5e5 / fps);
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    tracking::Gray prev;
    std::vector<Vec2> pos(pts.size());
    f32 sx = 1, sy = 1;
    i64 lastPts = std::numeric_limits<i64>::min();
    for (usize i = 0; i < targetUs.size(); ++i) {
        const i64 want = targetUs[i];
        if (i == 0 || want < lastPts - halfFrame) {
            if (!dec->seek_to_keyframe(want).ok()) break;
            lastPts = std::numeric_limits<i64>::min();
        }
        FrameRef frame;
        bool eos = false;
        for (int guard = 0; guard < 600 && !eos; ++guard) {
            FrameRef f;
            i64 pts64 = 0;
            if (!dec->next_frame(want - halfFrame, f, pts64, eos).ok()) { eos = true; break; }
            if (!f) continue;
            lastPts = pts64;
            if (pts64 >= want - halfFrame) { frame = std::move(f); break; }
        }
        if (!frame) break;
        ThumbnailService::Image img;
        if (!frame_to_thumbnail(*frame.get(), thumbH, img)) break;
        tracking::Gray g = tracking::to_gray(img.rgba.data(), img.width, img.height);
        u32 alive = 0;
        if (i == 0) {
            sx = static_cast<f32>(img.width) / static_cast<f32>(layerW);
            sy = static_cast<f32>(img.height) / static_cast<f32>(layerH);
            for (usize k = 0; k < pts.size(); ++k) pos[k] = Vec2{pts[k].x * sx, pts[k].y * sy};
            alive = static_cast<u32>(pts.size());
        } else {
            for (usize k = 0; k < pos.size(); ++k) {
                if (std::isnan(pos[k].x)) continue;
                const tracking::TrackStep st = tracking::track_step(prev, g, pos[k]);
                if (st.score < 0.6f) { pos[k] = Vec2{nan, nan}; continue; }   // perdido: fica de fora
                pos[k] = st.pos;
                ++alive;
            }
        }
        if (alive < std::max(1u, minAlive)) break;
        std::vector<Vec2> row(pos.size());
        for (usize k = 0; k < pos.size(); ++k) row[k] = std::isnan(pos[k].x) ? Vec2{nan, nan} : Vec2{pos[k].x / sx, pos[k].y / sy};
        out.push_back(std::move(row));
        prev = std::move(g);
    }
    AUREA_LOG_INFO("rastreio: %zu de %zu quadros (%zu pontos)", out.size(), targetUs.size(), pts.size());
    return true;
}

} // namespace

Result<u64> Engine::track_point(u64 layerId, f32 x, f32 y, bool stabilize, u32* trackedOut) noexcept {
    // 1. O que decodificar (sob o lock).
    Asset asset;
    i64 start = 0, end = 0;
    f64 fps = 30.0;
    std::vector<i64> targetUs;
    u32 layerW = 0, layerH = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->kind != LayerKind::Video) return Status{Errc::InvalidArgument, "rastreio precisa de camada de video"};
        const Asset* a = project_->asset(l->source);
        if (!a || !a->has_video()) return Status{Errc::InvalidArgument, "camada sem video"};
        asset = *a;
        asset.sourcePath = resolve_asset_path(asset.sourcePath);
        fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        // Do quadro do cabeçote (onde o ponto foi tocado) até o fim, como o
        // rastreio para a frente do AE; antes dele o Nulo fica no 1º ponto.
        start = std::clamp<i64>(playback_.current().value, l->start.value, std::max<i64>(l->start.value, l->end.value - 2));
        end = std::min<i64>(l->end.value, start + 3600);   // até 2 min a 30 fps por vez
        layerW = a->video.width;
        layerH = a->video.height;
        for (i64 f = start; f < end; ++f) {
            targetUs.push_back(static_cast<i64>(std::llround(std::max(0.0, l->source_frame(FrameIndex{f})) * 1e6 / fps)));
        }
    }
    VideoSourceFactory* factory = config_.mediaFactory;
    if (!factory || layerW == 0 || layerH == 0) return Status{Errc::InvalidState, "sem decodificador"};
    // 2. Quadro a quadro (NCC na miniatura), até o ponto se perder.
    std::vector<std::vector<Vec2>> followed;
    if (!follow_points(*factory, asset, targetUs, fps, layerW, layerH, {Vec2{x, y}}, 1, followed)) {
        return Status{Errc::IoError, "video ilegivel"};
    }
    std::vector<Vec2> track;   // px da camada, um por quadro rastreado
    for (const auto& f : followed) track.push_back(f[0]);
    if (trackedOut) *trackedOut = static_cast<u32>(track.size());
    if (track.size() < 2) return Status{Errc::InvalidArgument, "ponto sem textura para seguir"};
    AUREA_LOG_INFO("rastreio: %zu de %zu quadros", track.size(), targetUs.size());

    // 3. Keyframes (sob o lock, um passo de desfazer).
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return Status{Errc::NotFound, "camada sumiu"};
    history_.before_mutation(*comp, project_->timeline().current(), stabilize ? "estabilizar" : "rastrear ponto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    if (!stabilize) {
        const LayerId nid = comp->add_layer(LayerKind::Null, "Rastreio");
        Layer* n = comp->layer(nid);
        l = comp->layer(LayerId::unpack(layerId));
        if (!n || !l) return Status{Errc::OutOfMemory, "camada nao criada"};
        n->start = l->start;
        n->end = l->end;
        n->transform.anchor = Vec3{50, 50, 0};
        // Finish insertions before retaining references: another track may
        // grow the vector (especially after undo/copy of a compact TrackSet).
        (void)n->tracks.get_or_create(TrackProperty::PositionX);
        (void)n->tracks.get_or_create(TrackProperty::PositionY);
        Track& px = *n->tracks.find(TrackProperty::PositionX);
        Track& py = *n->tracks.find(TrackProperty::PositionY);
        for (usize i = 0; i < track.size(); ++i) {
            const FrameIndex f{start + static_cast<i64>(i)};
            const Vec4 w = layer_world_matrix(*comp, *l, f) * Vec4{track[i].x, track[i].y, 0, 1};
            const FrameIndex local = n->local_time(f);
            px.set(local, w.x);
            py.set(local, w.y);
        }
        n->transform.position = Vec3{px.keys.front().value, py.keys.front().value, 0};
        project_->mark_dirty();
        request_render();
        return nid.pack();
    }
    // Estabilizar: a camada anda o contrário do ponto (no espaço do pai, pela
    // rotação/escala dela) — somado à posição que ela já tinha em cada quadro.
    const f32 rz = l->transform.rotation.z * kDeg2Rad;
    const f32 c = std::cos(rz), s = std::sin(rz);
    const f32 kx = l->transform.scale.x, ky = l->transform.scale.y;
    Track base_x = l->tracks.find(TrackProperty::PositionX) ? *l->tracks.find(TrackProperty::PositionX) : Track{};
    Track base_y = l->tracks.find(TrackProperty::PositionY) ? *l->tracks.find(TrackProperty::PositionY) : Track{};
    // Finish insertions before retaining references: another track may
    // grow the vector (especially after undo/copy of a compact TrackSet).
    (void)l->tracks.get_or_create(TrackProperty::PositionX);
    (void)l->tracks.get_or_create(TrackProperty::PositionY);
    Track& px = *l->tracks.find(TrackProperty::PositionX);
    Track& py = *l->tracks.find(TrackProperty::PositionY);
    for (usize i = 0; i < track.size(); ++i) {
        const FrameIndex f{start + static_cast<i64>(i)};
        const FrameIndex local = l->local_time(f);
        const f32 dx = (track[0].x - track[i].x) * kx, dy = (track[0].y - track[i].y) * ky;
        const f32 bx = base_x.keys.empty() ? l->transform.position.x : base_x.sample_keys(local);
        const f32 by = base_y.keys.empty() ? l->transform.position.y : base_y.sample_keys(local);
        px.set(local, bx + dx * c - dy * s);
        py.set(local, by + dx * s + dy * c);
    }
    project_->mark_dirty();
    request_render();
    return layerId;
}

// =============================================================================
// Máscaras (roto) e track matte
// =============================================================================
namespace {

void read_points6(const f32* p, u32 n, std::vector<MaskPoint>& out) {
    out.resize(n);
    for (u32 i = 0; i < n; ++i) {
        const f32* q = p + static_cast<usize>(i) * 6;
        auto fin = [](f32 v) { return std::isfinite(v) ? v : 0.0f; };
        out[i].position = Vec2{fin(q[0]), fin(q[1])};
        out[i].inTangent = Vec2{fin(q[2]), fin(q[3])};
        out[i].outTangent = Vec2{fin(q[4]), fin(q[5])};
    }
}

Mask* mask_by_id(Layer& l, u32 id) noexcept {
    for (Mask& m : l.masks) if (m.id == id) return &m;
    return nullptr;
}

/// Canais escalares da máscara: suavização, expansão e opacidade.
f32* mask_param_ref(Mask& m, u32 param) noexcept {
    switch (param) { case 0: return &m.feather; case 1: return &m.expansion; case 2: return &m.opacity; default: return nullptr; }
}

void write_mask_param(Layer& l, Mask& m, u32 param, f32 value, FrameIndex time) noexcept {
    f32* base = mask_param_ref(m, param);
    if (!base || !std::isfinite(value)) return;
    value = std::clamp(value, param == 1 ? -2000.f : 0.f, param == 2 ? 1.f : 2000.f);
    Track* tr = l.tracks.find(TrackProperty::MaskParam, m.id, param);
    if (tr && !tr->keys.empty()) {
        const FrameIndex local = l.local_time(time);
        const u32 at = tr->find_exact(local);
        if (at != kInvalidIndex) tr->keys[at].value = value;
        else (void)tr->set(local, value, Interpolation::Bezier);
    } else *base = value;
    m.cacheKey = 0;
}

/// Key do caminho no instante local (índice), ou −1.
i32 path_key_at(const Mask& m, i64 local) noexcept {
    for (usize i = 0; i < m.pathKeys.size(); ++i) if (m.pathKeys[i].frame == local) return static_cast<i32>(i);
    return -1;
}

/// Grava a forma num key (cria em ordem se não houver key ali).
void put_path_key(Mask& m, i64 local, const std::vector<MaskPoint>& pts) {
    const i32 at = path_key_at(m, local);
    if (at >= 0) { m.pathKeys[static_cast<usize>(at)].points = pts; return; }
    MaskPathKey k;
    k.frame = local;
    k.points = pts;
    auto it = std::lower_bound(m.pathKeys.begin(), m.pathKeys.end(), local,
                               [](const MaskPathKey& a, i64 f) { return a.frame < f; });
    m.pathKeys.insert(it, std::move(k));
}

} // namespace

i32 Engine::add_mask(u64 layerId, const f32* pts6, u32 count, bool closed) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->masks.size() >= kMaxMaskCount || count > 4096 || (count > 0 && !pts6)) return -1;
    history_.before_mutation(*comp, project_->timeline().current(), "nova mascara");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    Mask m;
    m.id = l->alloc_mask_id();
    m.name = "Mascara " + std::to_string(m.id + 1);
    m.closed = closed;
    read_points6(pts6, count, m.points);
    l->masks.push_back(std::move(m));
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(l->masks.back().id);
}

bool Engine::remove_mask(u64 layerId, u32 maskId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !mask_by_id(*l, maskId)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "apagar mascara");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->masks.erase(std::remove_if(l->masks.begin(), l->masks.end(), [maskId](const Mask& m) { return m.id == maskId; }),
                   l->masks.end());
    l->tracks.remove_if([&](const Track& tr) { return tr.property == TrackProperty::MaskParam && tr.effectIndex == maskId; });
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_mask_path(u64 layerId, u32 maskId, const f32* pts6, u32 count, bool closed, bool undo) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    Mask* m = l ? mask_by_id(*l, maskId) : nullptr;
    if (!m || count > 4096 || (count > 0 && !pts6)) return false;
    if (undo) history_.before_mutation(*comp, project_->timeline().current(), "editar mascara");
    std::vector<MaskPoint> pts;
    read_points6(pts6, count, pts);
    // Caminho animado: a edição vira (ou atualiza) o key no cabeçote.
    if (!m->pathKeys.empty()) put_path_key(*m, l->local_time(playback_.current()).value, pts);
    else m->points = std::move(pts);
    m->closed = closed;
    m->cacheKey = 0;
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_mask_props(u64 layerId, u32 maskId, u32 op, bool inverted, f32 feather, f32 expansion, f32 opacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    Mask* m = l ? mask_by_id(*l, maskId) : nullptr;
    if (!m || op > static_cast<u32>(MaskOperation::None)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "ajustar mascara");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    m->operation = static_cast<MaskOperation>(op);
    m->inverted = inverted;
    const Vec3 current = mask::evaluate_props(*l, *m, static_cast<f64>(l->local_time(playback_.current()).value));
    if (feather != current.x) write_mask_param(*l, *m, 0, feather, playback_.current());
    if (expansion != current.y) write_mask_param(*l, *m, 1, expansion, playback_.current());
    if (opacity != current.z) write_mask_param(*l, *m, 2, opacity, playback_.current());
    m->cacheKey = 0;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_mask_param(u64 layerId, u32 maskId, u32 param, f32 value) noexcept {
    if (param > 2 || !std::isfinite(value)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    Mask* m = l ? mask_by_id(*l, maskId) : nullptr;
    if (!m || l->locked) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "valor da mascara");
    write_mask_param(*l, *m, param, value, playback_.current());
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty(); request_render();
    return true;
}

bool Engine::toggle_mask_param_key(u64 layerId, u32 maskId, u32 param) noexcept {
    if (param > 2) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    Mask* m = l ? mask_by_id(*l, maskId) : nullptr;
    if (!m || l->locked) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe da mascara");
    f32* base = mask_param_ref(*m, param);
    const FrameIndex local = l->local_time(playback_.current());
    Track& tr = l->tracks.get_or_create(TrackProperty::MaskParam, maskId, param);
    const u32 at = tr.find_exact(local);
    if (at != kInvalidIndex) {
        if (tr.keys.size() == 1) *base = tr.keys[0].value;
        (void)tr.remove(local);
    } else (void)tr.set(local, tr.keys.empty() ? *base : tr.sample_keys(local), Interpolation::Bezier);
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    m->cacheKey = 0; project_->mark_dirty(); request_render();
    return true;
}

bool Engine::ensure_mask_path_key(u64 layerId, u32 maskId, bool* keyedOut) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    Mask* m = l ? mask_by_id(*l, maskId) : nullptr;
    if (!m) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe da mascara");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const i64 local = l->local_time(playback_.current()).value;
    // Com keyframe aqui, regrava a forma AVALIADA — nunca apaga (ver
    // `ensure_shape_param_key`).
    std::vector<MaskPoint> shape;
    mask::evaluate_path(*m, static_cast<f64>(local), shape);
    put_path_key(*m, local, shape);
    m->cacheKey = 0;
    if (keyedOut) *keyedOut = true;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_mask_path_key(u64 layerId, u32 maskId, bool* keyedOut) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    Mask* m = l ? mask_by_id(*l, maskId) : nullptr;
    if (!m) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe da mascara");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const i64 local = l->local_time(playback_.current()).value;
    const i32 at = path_key_at(*m, local);
    bool keyed = false;
    if (at >= 0) {
        // Tirar o último key: a forma dele fica como o caminho parado.
        if (m->pathKeys.size() == 1) m->points = m->pathKeys[0].points;
        m->pathKeys.erase(m->pathKeys.begin() + at);
    } else {
        std::vector<MaskPoint> shape;
        mask::evaluate_path(*m, static_cast<f64>(local), shape);
        put_path_key(*m, local, shape);
        keyed = true;
    }
    m->cacheKey = 0;
    if (keyedOut) *keyedOut = keyed;
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::query_masks(u64 layerId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    const FrameIndex t = playback_.current();
    const i64 local = l->local_time(t).value;
    // Tamanho primeiro: quem chama realoca se não couber.
    std::vector<MaskPoint> pts;
    u32 need = 7;
    for (const Mask& m : l->masks) {
        mask::evaluate_path(m, static_cast<f64>(local), pts);
        need += kMaskHeaderFloats + static_cast<u32>(pts.size()) * 6;
    }
    if (!out || capacity < need) return need;
    const Mat4 w = layer_comp_matrix(*comp, *l, t);
    out[0] = w.col[0].x; out[1] = w.col[0].y; out[2] = w.col[1].x; out[3] = w.col[1].y; out[4] = w.col[3].x; out[5] = w.col[3].y;
    out[6] = static_cast<f32>(l->masks.size());
    u32 o = 7;
    for (const Mask& m : l->masks) {
        mask::evaluate_path(m, static_cast<f64>(local), pts);
        f32* h = out + o;
        h[0] = static_cast<f32>(m.id);
        h[1] = static_cast<f32>(static_cast<u8>(m.operation));
        h[2] = m.inverted ? 1.0f : 0.0f;
        const Vec3 props = mask::evaluate_props(*l, m, static_cast<f64>(local));
        h[3] = props.x;
        h[4] = props.y;
        h[5] = props.z;
        h[6] = m.closed ? 1.0f : 0.0f;
        h[7] = static_cast<f32>(pts.size());
        h[8] = static_cast<f32>(m.pathKeys.size());
        h[9] = path_key_at(m, local) >= 0 ? 1.0f : 0.0f;
        h[10] = mask::active(m) ? 1.0f : 0.0f;
        h[11] = 0.0f;
        o += kMaskHeaderFloats;
        for (const MaskPoint& p : pts) {
            out[o++] = p.position.x; out[o++] = p.position.y;
            out[o++] = p.inTangent.x; out[o++] = p.inTangent.y;
            out[o++] = p.outTangent.x; out[o++] = p.outTangent.y;
        }
    }
    return o;
}

Result<u32> Engine::track_mask(u64 layerId, u32 maskId, u32 mode) noexcept {
    // 1. O que decodificar e a forma de partida (sob o lock).
    Asset asset;
    i64 start = 0, end = 0, localStart = 0;
    f64 fps = 30.0;
    std::vector<i64> targetUs;
    u32 layerW = 0, layerH = 0;
    std::vector<MaskPoint> base;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = project_ ? current_composition() : nullptr;
        Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->kind != LayerKind::Video) return Status{Errc::InvalidArgument, "rastreio de mascara precisa de camada de video"};
        Mask* m = mask_by_id(*l, maskId);
        if (!m) return Status{Errc::NotFound, "mascara nao existe"};
        const Asset* a = project_->asset(l->source);
        if (!a || !a->has_video()) return Status{Errc::InvalidArgument, "camada sem video"};
        asset = *a;
        asset.sourcePath = resolve_asset_path(asset.sourcePath);
        fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        start = std::clamp<i64>(playback_.current().value, l->start.value, std::max<i64>(l->start.value, l->end.value - 2));
        end = std::min<i64>(l->end.value, start + 3600);
        localStart = l->local_time(FrameIndex{start}).value;
        mask::evaluate_path(*m, static_cast<f64>(localStart), base);
        layerW = a->video.width;
        layerH = a->video.height;
        for (i64 f = start; f < end; ++f) {
            targetUs.push_back(static_cast<i64>(std::llround(std::max(0.0, l->source_frame(FrameIndex{f})) * 1e6 / fps)));
        }
    }
    if (base.size() < 2) return Status{Errc::InvalidArgument, "mascara sem caminho"};
    VideoSourceFactory* factory = config_.mediaFactory;
    if (!factory || layerW == 0 || layerH == 0) return Status{Errc::InvalidState, "sem decodificador"};
    // 2. Pontos seguidos: o centro da máscara e quatro por dentro da caixa dela
    // (a borda da máscara costuma cair na borda do objeto, onde o fundo engana).
    Vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
    for (const MaskPoint& p : base) {
        lo = Vec2{std::min(lo.x, p.position.x), std::min(lo.y, p.position.y)};
        hi = Vec2{std::max(hi.x, p.position.x), std::max(hi.y, p.position.y)};
    }
    const Vec2 c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f};
    const f32 hx = std::max(12.0f, (hi.x - lo.x) * 0.5f) * 0.4f, hy = std::max(12.0f, (hi.y - lo.y) * 0.5f) * 0.4f;
    std::vector<Vec2> feats = {c, {c.x - hx, c.y - hy}, {c.x + hx, c.y - hy}, {c.x + hx, c.y + hy}, {c.x - hx, c.y + hy}};
    for (Vec2& f : feats) {
        f.x = std::clamp(f.x, 9.0f, static_cast<f32>(layerW) - 10.0f);
        f.y = std::clamp(f.y, 9.0f, static_cast<f32>(layerH) - 10.0f);
    }
    const bool similarity = mode == 1;
    std::vector<std::vector<Vec2>> followed;
    if (!follow_points(*factory, asset, targetUs, fps, layerW, layerH, feats, similarity ? 2u : 1u, followed)) {
        return Status{Errc::IoError, "video ilegivel"};
    }
    if (followed.size() < 2) return Status{Errc::InvalidArgument, "mascara sem textura para seguir"};
    // 3. Forma por quadro: translação média (modo 0) ou semelhança por mínimos
    // quadrados (Umeyama 2D: escala + giro + translação) dos pontos vivos.
    std::vector<std::vector<MaskPoint>> shapes(followed.size());
    for (usize i = 0; i < followed.size(); ++i) {
        Vec2 oc{0, 0}, pc{0, 0};
        u32 n = 0;
        for (usize k = 0; k < feats.size(); ++k) {
            if (std::isnan(followed[i][k].x)) continue;
            oc = Vec2{oc.x + followed[0][k].x, oc.y + followed[0][k].y};
            pc = Vec2{pc.x + followed[i][k].x, pc.y + followed[i][k].y};
            ++n;
        }
        if (n == 0) { shapes[i] = i > 0 ? shapes[i - 1] : base; continue; }
        const f32 fn = static_cast<f32>(n);
        oc = Vec2{oc.x / fn, oc.y / fn};
        pc = Vec2{pc.x / fn, pc.y / fn};
        f32 sc = 1.0f, ss = 0.0f;   // s·cos, s·sin
        if (similarity && n >= 2) {
            f32 a = 0, b = 0, den = 0;
            for (usize k = 0; k < feats.size(); ++k) {
                if (std::isnan(followed[i][k].x)) continue;
                const f32 ox = followed[0][k].x - oc.x, oy = followed[0][k].y - oc.y;
                const f32 px = followed[i][k].x - pc.x, py = followed[i][k].y - pc.y;
                a += ox * px + oy * py;
                b += ox * py - oy * px;
                den += ox * ox + oy * oy;
            }
            if (den > 1.0f) { sc = a / den; ss = b / den; }
        }
        auto lin = [sc, ss](Vec2 v) { return Vec2{sc * v.x - ss * v.y, ss * v.x + sc * v.y}; };
        shapes[i].resize(base.size());
        for (usize k = 0; k < base.size(); ++k) {
            const Vec2 r = lin(Vec2{base[k].position.x - oc.x, base[k].position.y - oc.y});
            shapes[i][k].position = Vec2{r.x + pc.x, r.y + pc.y};
            shapes[i][k].inTangent = lin(base[k].inTangent);
            shapes[i][k].outTangent = lin(base[k].outTangent);
        }
    }
    // 4. Keys do caminho (um passo de desfazer): os do trecho rastreado são trocados.
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    Mask* m = l ? mask_by_id(*l, maskId) : nullptr;
    if (!m) return Status{Errc::NotFound, "mascara sumiu"};
    history_.before_mutation(*comp, project_->timeline().current(), "rastrear mascara");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const i64 localEnd = localStart + static_cast<i64>(shapes.size());
    m->pathKeys.erase(std::remove_if(m->pathKeys.begin(), m->pathKeys.end(),
                                     [&](const MaskPathKey& k) { return k.frame >= localStart && k.frame < localEnd; }),
                      m->pathKeys.end());
    for (usize i = 0; i < shapes.size(); ++i) put_path_key(*m, localStart + static_cast<i64>(i), shapes[i]);
    m->cacheKey = 0;
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(shapes.size());
}

bool Engine::set_track_matte(u64 layerId, u64 matteLayerId, u32 mode) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || mode > static_cast<u32>(MatteMode::LumaInverted)) return false;
    const bool clear = mode == 0 || matteLayerId == 0;
    if (!clear && (matteLayerId == layerId || !comp->layer(LayerId::unpack(matteLayerId)))) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "track matte");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->matteSource = clear ? LayerId{} : LayerId::unpack(matteLayerId);
    l->matteMode = clear ? MatteMode::None : static_cast<MatteMode>(mode);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_track_matte(u64 layerId, u64& matte, u32& mode) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    const bool ok = l->matteMode != MatteMode::None && l->matteSource.valid() && comp->layer(l->matteSource);
    matte = ok ? l->matteSource.pack() : 0;
    mode = ok ? static_cast<u32>(l->matteMode) : 0;
    return true;
}

// =============================================================================
// Rastreio de câmera 3D
// =============================================================================
void Engine::join_camera_track() noexcept {
    if (!cameraTrack_) return;
    cameraTrack_->cancel.store(true);
    if (cameraTrack_->thread.joinable()) cameraTrack_->thread.join();
}

bool Engine::restore_camera_track(u64 layerId) noexcept {
    if (cameraTrack_ && !cameraTrack_->finished.load()) return false;
    std::shared_ptr<const CameraTrackResult> saved;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        const Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->kind != LayerKind::Video || !l->cameraTrack) return false;
        saved = l->cameraTrack;
        if (saved->sourceSignature != tracking::source_signature(project_->asset(l->source)) ||
            !tracking::camera_timing_matches(*l,*saved,comp->fps())) return false;
        if (cameraTrack_ && cameraTrack_->layerId == layerId && cameraTrack_->session == projectSession_ &&
            cameraTrack_->composition == project_->timeline().current() && cameraTrack_->result == saved) return true;
    }
    // start_camera_track only returns cached data when key, mode and timing
    // match. Do not launch work on a stale cache from panel appearance.
    join_camera_track();
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->cameraTrack != saved) return false;
    cameraTrack_ = std::make_unique<CameraTrackJob>();
    auto& job = *cameraTrack_;
    job.layerId = layerId; job.source = l->source; job.start = l->start.value;
    job.composition = project_->timeline().current(); job.session = projectSession_;
    job.cacheKey = saved->cacheKey; job.cached = true; job.result = saved;
    job.state.store(saved->solution.ok ? 2u : 3u); job.progress.store(1.0f);
    job.message = saved->solution.failure;
    return true;
}

bool Engine::start_camera_track(u64 layerId, u32 mode, u32 motion, f32 knownFov) noexcept {
    if (motion_track_status().state == 1) return false;
    if (mode > 2 || motion > 2 || !std::isfinite(knownFov) || knownFov < 0 || knownFov > 150) return false;
    // Uma análise por vez; uma CANCELADA ainda saindo não bloqueia a próxima
    // (decode e solve olham o cancelamento a cada quadro/iteração).
    if (cameraTrack_ && !cameraTrack_->finished.load() && !cameraTrack_->cancel.load()) return false;
    join_camera_track();
    bool tooLong = false;
    Asset asset;
    std::vector<i64> targetUs;
    i64 start = 0;
    u32 analysisH = 0;
    u64 key = 1469598103934665603ull;
    u64 sourceSignature = 0;
    std::shared_ptr<const CameraTrackResult> saved;
    u64 session = 0;
    CompositionId composition{};
    AssetId source{};
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->kind != LayerKind::Video) return false;
        tooLong = l->duration().value > tracking::kMaxCameraTrackFrames;
        source = l->source;
        const Asset* a = project_->asset(l->source);
        if (!a || !a->has_video() || a->video.height == 0) return false;
        sourceSignature = tracking::source_signature(a);
        session = projectSession_;
        composition = project_->timeline().current();
        asset = *a;
        asset.sourcePath = resolve_asset_path(asset.sourcePath);
        const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        start = l->start.value;
        const i64 end = std::min<i64>(l->end.value, start + 1800);   // até 1 min a 30 fps por análise
        for (i64 f = start; f < end; ++f)
            targetUs.push_back(static_cast<i64>(std::llround(std::max(0.0, l->source_frame(FrameIndex{f})) * 1e6 / fps)));
        // Proxy de análise: 360 / 540 / 720 px de altura (nunca acima do vídeo).
        analysisH = std::min<u32>(a->video.height, mode == 0 ? 360u : (mode == 2 ? 720u : 540u));
        analysisH = std::max(1u,std::min(analysisH,static_cast<u32>(1440ull*a->video.height/a->video.width)));
        // Cache: vídeo (conteúdo), trecho, tempo, resolução, versão e modo.
        auto mix = [&](const void* p, usize n) { const u8* b = static_cast<const u8*>(p); for (usize i = 0; i < n; ++i) { key ^= b[i]; key *= 1099511628211ull; } };
        mix(&sourceSignature,sizeof(sourceSignature));
        mix(&a->contentHash, sizeof(a->contentHash));
        // Moving a project between platforms must not invalidate known media.
        // Sources without a fingerprint conservatively retain the path key.
        if (!a->contentHash) mix(a->sourcePath.data(), a->sourcePath.size());
        mix(&a->video.width, sizeof(a->video.width)); mix(&a->video.height, sizeof(a->video.height));
        for (i64 us : targetUs) mix(&us, sizeof(us));
        mix(&analysisH, sizeof(analysisH));
        mix(&mode, sizeof(mode)); mix(&motion, sizeof(motion)); mix(&knownFov, sizeof(knownFov));
        const u32 ver = kCameraTrackerVersion;
        mix(&ver, sizeof(ver));
        if (l->cameraTrack && l->cameraTrack->sourceSignature == sourceSignature && l->cameraTrack->cacheKey == key && l->cameraTrack->sourceUs == targetUs)
            saved = l->cameraTrack;
    }
    if (targetUs.size() < 10) return false;
    cameraTrack_ = std::make_unique<CameraTrackJob>();
    CameraTrackJob* job = cameraTrack_.get();
    if (tooLong) {
        // Antes: recusa muda e a UI dizia "precisa de 10 quadros". O solve
        // cresce com os quadros; a câmera cobre a camada inteira. Diz o porquê.
        job->layerId = layerId; job->session = session; job->composition = composition; job->source = source;
        job->message = "Clipe longo demais para a câmera 3D (até " + std::to_string(tracking::kMaxCameraTrackFrames) +
                       " quadros por análise). Divida o clipe no cabeçote e analise cada parte.";
        job->state.store(3u);
        job->progress.store(1.0f);
        return true;
    }
    job->layerId = layerId;
    job->start = start;
    job->cacheKey = key;
    job->session = session; job->composition = composition; job->source = source;
    if (saved) {
        job->result = saved;
        job->cached = true;
        job->progress.store(1.0f);
        job->state.store(job->result->solution.ok ? 2u : 3u);
        job->message = job->result->solution.failure;
        return true;
    }
    job->state.store(1);
    job->finished.store(false);
    VideoSourceFactory* factory = config_.mediaFactory;
    job->thread = std::thread([this, job, asset, targetUs, analysisH, mode, motion, knownFov, factory, sourceSignature]() {
        struct Completion { CameraTrackJob* job; ~Completion() { job->finished.store(true); } } completion{job};
        auto fail = [&](const char* why) {
            std::lock_guard<std::mutex> g(job->mutex);
            job->message = why;
            job->state.store(job->cancel.load() ? 4u : 3u);
        };
        if (!factory) return fail("sem decodificador");
        auto dec = factory->open_video(asset, MediaPriority::Thumbnail);
        if (!dec) return fail("video ilegivel");
        const tracking::TrackMode tm = mode == 0 ? tracking::TrackMode::Fast : (mode == 2 ? tracking::TrackMode::High : tracking::TrackMode::Balanced);
        tracking::FeatureTracker ft(tm);
        const f64 fps = asset.timebaseFps > 0.0 ? asset.timebaseFps : 30.0;
        // O mesmo quadro que o preview mostra em cada instante (VFR incluso) e
        // luma por área (sem o serrilhado da miniatura por vizinho mais próximo).
        tracking::AnalysisFramePicker picker(*dec, fps, job->cancel);
        u32 aw = 0, ah = 0;
        for (usize i = 0; i < targetUs.size(); ++i) {
            if (job->cancel.load()) return fail("cancelado");
            FrameRef frame;
            const auto picked = picker.pick(targetUs[i], frame);
            if (picked == tracking::AnalysisFramePicker::Result::Cancelled) return fail("cancelado");
            if (picked != tracking::AnalysisFramePicker::Result::Ok || !frame) break;
            tracking::Gray gray;
            if (!tracking::frame_to_gray(*frame.get(), analysisH, gray)) break;
            frame = {};
            aw = gray.width;
            ah = gray.height;
            ft.add_frame(gray);
            if (static_cast<u64>(ft.tracks().pos.size()) * targetUs.size() > tracking::kMaxCameraTrackCells)
                return fail("trecho excede o limite de memoria de analise; escolha um trecho menor");
            job->progress.store(0.5f * static_cast<f32>(i + 1) / static_cast<f32>(targetUs.size()));
        }
        if (ft.tracks().frames < 10) return fail("nao deu para ler quadros suficientes do video");
        if (ft.tracks().frames != targetUs.size()) return fail("video incompleto: nao foi possivel analisar todo o trecho");
        tracking::SolveOptions opt;
        opt.mode = tm; opt.motion = motion; opt.knownFovDeg = knownFov;
        std::atomic<f32> solveProgress{0.0f};
        // O solve informa 0..1; a análise mostra 0,5..1.
        std::thread watcher([&] {
            while (job->state.load() == 1 && solveProgress.load() < 1.0f && !job->cancel.load()) {
                job->progress.store(0.5f + 0.5f * solveProgress.load());
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        });
        tracking::CameraSolution sol = tracking::solve_camera(ft.tracks(), opt, &job->cancel, &solveProgress);
        solveProgress.store(1.0f);
        watcher.join();
        if (job->cancel.load()) return fail("cancelado");
        auto res = std::make_shared<CameraTrackResult>();
        res->solution = std::move(sol);
        res->tracks = ft.tracks();
        res->frames = ft.tracks().frames;
        res->analysisW = aw;
        res->analysisH = ah;
        res->cacheKey = job->cacheKey; res->mode = mode; res->sourceUs = targetUs;
        res->sourceSignature = sourceSignature;
        if (res->solution.ok) {
            std::lock_guard<std::mutex> lock(modelMutex_);
            Composition* comp = project_ && projectSession_ == job->session ? project_->timeline().composition(job->composition) : nullptr;
            Layer* layer = comp ? comp->layer(LayerId::unpack(job->layerId)) : nullptr;
            if (layer && layer->kind == LayerKind::Video && layer->source == job->source &&
                res->sourceSignature == tracking::source_signature(project_->asset(layer->source)) &&
                tracking::camera_timing_matches(*layer,*res,comp->fps())) {
                // Immutable cache snapshots are shared by history and save.
                layer->cameraTrack = res;
                project_->mark_dirty();
            } else return fail("Source or timing changed during analysis");
        }
        std::lock_guard<std::mutex> g(job->mutex);
        job->result = res;
        job->message = res->solution.ok ? (res->solution.rotationOnly ? "camera parada no lugar (so gira): sem profundidade" : "") : res->solution.failure;
        job->progress.store(1.0f);
        job->state.store(res->solution.ok ? 2u : 3u);
        AUREA_LOG_INFO("rastreio de camera: %s, %u/%u quadros, %u pontos, erro %.2f px, FOV %.1f",
                       res->solution.ok ? "ok" : "falhou", res->solution.framesSolved, res->frames, res->solution.inliers,
                       static_cast<double>(res->solution.rmsError), static_cast<double>(res->solution.fovY / kDeg2Rad));
    });
    return true;
}

bool Engine::refine_camera_track(bool removeSelected, u32 motion, f32 knownFov) noexcept {
    if (!cameraTrack_ || !cameraTrack_->finished.load() || cameraTrack_->state.load()!=2 || motion>2 ||
        !std::isfinite(knownFov) || knownFov<0 || knownFov>150) return false;
    join_camera_track();
    auto* job=cameraTrack_.get();
    std::shared_ptr<const CameraTrackResult> original;
    std::vector<u32> rejected;
    { std::lock_guard<std::mutex> g(job->mutex);original=job->result;rejected=job->selectedTracks; }
    if (!original || (removeSelected && rejected.empty())) return false;
    job->cancel.store(false);job->finished.store(false);job->state.store(1);job->progress.store(0);
    job->thread=std::thread([this,job,original,rejected,removeSelected,motion,knownFov] {
        struct Done { CameraTrackJob* j; ~Done(){j->finished.store(true);} } done{job};
        auto next=std::make_shared<CameraTrackResult>(*original);
        if(removeSelected)for(u32 id:rejected)if(id<next->tracks.pos.size())std::fill(next->tracks.pos[id].begin(),next->tracks.pos[id].end(),Vec2{NAN,NAN});
        tracking::SolveOptions opt;opt.mode=static_cast<tracking::TrackMode>(next->mode);opt.motion=motion;opt.knownFovDeg=knownFov;
        auto solution=tracking::solve_camera(next->tracks,opt,&job->cancel,&job->progress);
        if(job->cancel.load()){job->state.store(4);return;}
        if(!solution.ok){std::lock_guard<std::mutex> g(job->mutex);job->message=solution.failure;job->state.store(3);return;}
        next->solution=std::move(solution);next->sceneCalibration=Mat4::identity();
        next->cacheKey^=0x9e3779b97f4a7c15ull;for(auto id:rejected)next->cacheKey=(next->cacheKey^id)*1099511628211ull;
        next->cacheKey^=static_cast<u64>(knownFov*10000)+motion;
        {
            std::lock_guard<std::mutex> g(modelMutex_);
            auto* comp=project_&&projectSession_==job->session?project_->timeline().composition(job->composition):nullptr;
            auto* layer=comp?comp->layer(LayerId::unpack(job->layerId)):nullptr;
            if(!layer||layer->cameraTrack!=original||layer->source!=job->source||original->sourceSignature!=tracking::source_signature(project_->asset(layer->source))||!tracking::camera_timing_matches(*layer,*original,comp->fps())){std::lock_guard<std::mutex> jg(job->mutex);job->message="Source or timing changed during solve";job->state.store(3);return;}
            history_.before_mutation(*comp,job->composition,"Refine camera solve");
            layer->cameraTrack=next;modelRevision_.fetch_add(1,std::memory_order_acq_rel);project_->mark_dirty();
        }
        std::lock_guard<std::mutex>g(job->mutex);job->result=next;job->cacheKey=next->cacheKey;job->selectedTracks.clear();job->appliedPoints.clear();job->message="New solve ready. Apply the camera and place new anchors for this solve.";job->cached=true;job->progress.store(1);job->state.store(2);
    });
    return true;
}

void Engine::cancel_camera_track() noexcept {
    if (!cameraTrack_) return;
    cameraTrack_->cancel.store(true);
    // Signal only: decoder/solver cleanup stays on the worker, never the UI.
    if (cameraTrack_->state.load() == 1) cameraTrack_->state.store(4);
}

Engine::CameraTrackStatus Engine::camera_track_status() noexcept {
    CameraTrackStatus st;
    if (!cameraTrack_) return st;
    CameraTrackJob* job = cameraTrack_.get();
    st.state = job->state.load();
    st.progress = job->progress.load();
    std::lock_guard<std::mutex> g(job->mutex);
    st.message = job->message;
    st.cached = job->cached;
    if (job->result) {
        const tracking::CameraSolution& s = job->result->solution;
        st.frames = job->result->frames;
        st.framesSolved = s.framesSolved;
        st.tracks = s.tracks;
        st.inliers = s.inliers;
        st.rmsError = s.rmsError;
        st.confidence = s.confidence;
        st.fovDeg = s.fovY / kDeg2Rad;
        st.rotationOnly = s.rotationOnly;
        if (job->state.load() == 2 && job->thread.joinable()) {
            // Terminou: guarda no cache (reanalisar o mesmo vídeo é instantâneo).
            // Result already belongs to the video layer and is serialized.
        }
    }
    return st;
}

Result<u64> Engine::apply_camera_track(i64 selectionFrame, Vec4 selectionRect, u32 object) noexcept {
    if (object > 4) return Status{Errc::InvalidArgument, "tipo de objeto invalido"};
    if (!cameraTrack_ || cameraTrack_->state.load() != 2) return Status{Errc::InvalidState, "rastreio nao terminou"};
    CameraTrackJob* job = cameraTrack_.get();
    if (job->thread.joinable()) job->thread.join();
    std::shared_ptr<const CameraTrackResult> res;
    {
        std::lock_guard<std::mutex> g(job->mutex);
        res = job->result;
    }
    if (!res || !res->solution.ok) return Status{Errc::InvalidState, "sem solucao"};
    const tracking::CameraSolution& s = res->solution;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* video = comp ? comp->layer(LayerId::unpack(job->layerId)) : nullptr;
    if (!video) return Status{Errc::NotFound, "camada sumiu"};
    if (projectSession_ != job->session || project_->timeline().current() != job->composition)
        return Status{Errc::InvalidState, "a analise pertence a outra composicao"};
    if (video->cameraTrack != res || video->source != job->source || video->duration().value != res->frames)
        return Status{Errc::InvalidState, "tempo do video mudou; reanalise o trecho"};
    for (u32 i = 0; i < res->frames; ++i) {
        const i64 us = static_cast<i64>(std::llround(std::max(0.0, video->source_frame(FrameIndex{video->start.value + i})) * 1e6 / comp->fps()));
        if (us != res->sourceUs[i]) return Status{Errc::InvalidState, "tempo do video mudou; reanalise o trecho"};
    }
    job->start = video->start.value;
    const Asset* source = project_->asset(video->source);
    if (!source || source->video.height == 0) return Status{Errc::InvalidState, "video sem dimensoes"};
    if(res->sourceSignature!=tracking::source_signature(source))return Status{Errc::InvalidState,"Source changed: analyse again"};
    const Mat4 videoMatrix = layer_comp_matrix(*comp, *video, FrameIndex{job->start});
    const f32 videoScale = std::hypot(videoMatrix.col[1].x, videoMatrix.col[1].y);
    const f32 displayedHeight = source->video.height * videoScale;
    if (displayedHeight < 1e-6f) return Status{Errc::InvalidArgument, "video sem escala"};
    const f32 compositionFov = 2.0f * std::atan(static_cast<f32>(comp->height()) / displayedHeight * std::tan(s.fovY * 0.5f));
    std::vector<usize> selectedPoints;
    if (!job->selectedTracks.empty()) {
        usize point = 0;
        for (usize track = 0; track < s.trackSolved.size(); ++track) {
            if (!s.trackSolved[track] || s.rotationOnly) continue;
            const usize index = point++;
            if (index < s.points.size() && std::find(job->selectedTracks.begin(), job->selectedTracks.end(), track) != job->selectedTracks.end())
                selectedPoints.push_back(index);
        }
        if (selectedPoints.empty() && object != 0) return Status{Errc::InvalidArgument, "selecione pontos resolvidos"};
    } else if (selectionFrame >= 0) {
        const i64 frame = selectionFrame - job->start;
        const Asset* asset = project_->asset(video->source);
        if (!asset || frame < 0 || frame >= res->tracks.frames || s.rotationOnly)
            return Status{Errc::InvalidArgument, "selecione pontos 3D resolvidos no trecho analisado"};
        const Mat4 matrix = layer_comp_matrix(*comp, *video, FrameIndex{selectionFrame});
        const f32 kx = static_cast<f32>(asset->video.width) / res->tracks.width;
        const f32 ky = static_cast<f32>(asset->video.height) / res->tracks.height;
        const f32 left = std::min(selectionRect.x, selectionRect.z), right = std::max(selectionRect.x, selectionRect.z);
        const f32 top = std::min(selectionRect.y, selectionRect.w), bottom = std::max(selectionRect.y, selectionRect.w);
        usize point = 0;
        for (usize t = 0; t < s.trackSolved.size(); ++t) {
            if (!s.trackSolved[t]) continue;
            const usize index = point++;
            if (index >= s.points.size() || t >= res->tracks.pos.size()) continue;
            const Vec2 p = res->tracks.pos[t][static_cast<usize>(frame)];
            if (!tracking::Tracks2D::present(p)) continue;
            const Vec4 q = matrix * Vec4{p.x * kx, p.y * ky, 0, 1};
            if (q.w > 1e-6f && q.x / q.w >= left && q.x / q.w <= right && q.y / q.w >= top && q.y / q.w <= bottom)
                selectedPoints.push_back(index);
        }
        if (selectedPoints.empty()) return Status{Errc::InvalidArgument, "nenhum ponto 3D selecionado"};
    }
    history_.before_mutation(*comp, project_->timeline().current(), "camera rastreada");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const f32 w = static_cast<f32>(comp->width()), h = static_cast<f32>(comp->height());
    // A câmera do 1º quadro vira a câmera padrão da composição (mesmo lugar,
    // olhando para +Z); a mediana da profundidade dos pontos cai no plano Z = 0.
    const scene3d::SceneCamera def = scene3d::default_camera(comp->width(), comp->height());
    const Vec3 C0 = def.position;
    const f32 dist = -C0.z;
    std::vector<f32> depth;
    const tracking::CameraPose* first = nullptr;
    for (const auto& p : s.poses) if (p.valid) { first = &p; break; }
    for (const Vec3& X : s.points) {
        if (!first) break;
        const f64 z = first->R[6] * X.x + first->R[7] * X.y + first->R[8] * X.z + first->t[2];
        if (z > 0) depth.push_back(static_cast<f32>(z));
    }
    f32 scale = 1.0f;
    if (!depth.empty()) {
        std::nth_element(depth.begin(), depth.begin() + static_cast<long>(depth.size() / 2), depth.end());
        scale = dist / std::max(1e-6f, depth[depth.size() / 2]);
    }
    auto baseComp = [&](Vec3 X) { return Vec3{X.x * scale + C0.x, X.y * scale + C0.y, X.z * scale + C0.z}; };
    auto toComp = [&](Vec3 X) { auto p=baseComp(X);const auto q=res->sceneCalibration*Vec4{p.x,p.y,p.z,1};return Vec3{q.x,q.y,q.z}; };
    // Outras câmeras ativas saem (a rastreada manda).
    for (u32 i = 0; i < comp->order().size(); ++i) {
        Layer* x = comp->layer(comp->order().at(i));
        if (x && x->kind == LayerKind::Camera) x->camera.active = false;
    }
    LayerId cid{};
    comp->layers().for_each([&](LayerId id, const Layer& layer) {
        if (layer.kind == LayerKind::Camera && layer.cameraTrackSource == LayerId::unpack(job->layerId)) cid = id;
    });
    const bool createCamera = !cid.valid() || comp->layer(cid)->cameraTrackKey != job->cacheKey;
    if (!cid.valid()) cid = comp->add_layer(LayerKind::Camera, "Câmera rastreada");
    Layer* cam = comp->layer(cid);
    video = comp->layer(LayerId::unpack(job->layerId));
    if (!cam || !video) return Status{Errc::OutOfMemory, "camada nao criada"};
    if (createCamera) {
    cam->start = video->start;
    cam->end = video->end;
    cam->camera.active = true;
    cam->cameraTrackSource = LayerId::unpack(job->layerId); cam->cameraTrackKey = job->cacheKey;
    cam->camera.fov = s.fovY / kDeg2Rad;
    cam->camera.focalLength = camera_focal_from_fov(cam->camera.fov);
    cam->camera.trackingSourceSize = {static_cast<f32>(source->video.width), static_cast<f32>(source->video.height)};
    cam->camera.nearPlane = std::max(1.0f, dist * 0.01f);
    cam->transform.anchor = Vec3{0, 0, 0};
    cam->transform.scale = Vec3{1, 1, 1};
    cam->parentBasis = res->sceneCalibration; cam->hasParentBasis = true;
    for (TrackProperty property : {TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ,
                                   TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ}) {
        cam->tracks.get_or_create(property).clear();
    }
    Track& px = *cam->tracks.find(TrackProperty::PositionX);
    Track& py = *cam->tracks.find(TrackProperty::PositionY);
    Track& pz = *cam->tracks.find(TrackProperty::PositionZ);
    Track& rx = *cam->tracks.find(TrackProperty::RotationX);
    Track& ry = *cam->tracks.find(TrackProperty::RotationY);
    Track& rz = *cam->tracks.find(TrackProperty::RotationZ);
    Vec3 prevE{0, 0, 0};
    bool havePrev = false;
    for (u32 i = 0; i < s.poses.size(); ++i) {
        const tracking::CameraPose& p = s.poses[i];
        if (!p.valid) continue;
        const FrameIndex local = cam->local_time(FrameIndex{job->start + static_cast<i64>(i)});
        const Vec3 c = baseComp(p.center());
        // Câmera → mundo = Rᵀ (colunas = eixos da câmera).
        const f64 Rwc[9] = {p.R[0], p.R[3], p.R[6], p.R[1], p.R[4], p.R[7], p.R[2], p.R[5], p.R[8]};
        Vec3 e = tracking::euler_zyx_from_matrix(Rwc) * (1.0f / kDeg2Rad);
        if (havePrev) {   // sem pulo de 360° entre quadros
            auto unwrap = [](f32 v, f32 ref) { while (v - ref > 180.0f) v -= 360.0f; while (v - ref < -180.0f) v += 360.0f; return v; };
            e = Vec3{unwrap(e.x, prevE.x), unwrap(e.y, prevE.y), unwrap(e.z, prevE.z)};
        }
        prevE = e;
        havePrev = true;
        px.set(local, c.x);
        py.set(local, c.y);
        pz.set(local, c.z);
        rx.set(local, e.x);
        ry.set(local, e.y);
        rz.set(local, e.z);
    }
    if (!px.keys.empty()) {
        cam->transform.position = Vec3{px.keys.front().value, py.keys.front().value, pz.keys.front().value};
        cam->transform.rotation = Vec3{rx.keys.front().value, ry.keys.front().value, rz.keys.front().value};
    }
    } // Keep user-edited camera keys when adding another tracked object.
    cam->camera.active = true;
    // Referência da cena: chão (plano dominante) ou o centro dos pontos.
    job->appliedPoints.clear();
    for (const Vec3& X : s.points) job->appliedPoints.push_back(toComp(X));
    if (!job->appliedPoints.empty() && object != 0) {
        std::vector<Vec3> referencePoints;
        if (!selectedPoints.empty()) {
            for (usize index : selectedPoints) referencePoints.push_back(job->appliedPoints[index]);
        } else { referencePoints = job->appliedPoints; }
        Vec3 centroid{}, normal{};
        const f32 sceneScale=Vec3{res->sceneCalibration.col[0].x,res->sceneCalibration.col[0].y,res->sceneCalibration.col[0].z}.length();
        const bool plane = tracking::dominant_plane(referencePoints, dist * sceneScale * 0.02f, 0.3f, centroid, normal);
        if (!plane) {
            std::vector<f32> xs, ys, zs;
            for (const Vec3& q : referencePoints) { xs.push_back(q.x); ys.push_back(q.y); zs.push_back(q.z); }
            auto med = [](std::vector<f32>& v) { std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end()); return v[v.size() / 2]; };
            centroid = Vec3{med(xs), med(ys), med(zs)};
        }
        const FrameIndex cameraStart = cam->start, cameraEnd = cam->end;
        const LayerKind kind = object == 2 || object == 4 ? LayerKind::Shape : object == 3 ? LayerKind::Text : LayerKind::Null;
        const LayerId nid = comp->add_layer(kind, object == 2 ? "Tracked shape" : object == 3 ? "Tracked text" : object == 4 ? "Tracked solid" : "Tracked anchor");
        if (Layer* n = comp->layer(nid)) {
            n->cameraTrackSource = LayerId::unpack(job->layerId); n->cameraTrackKey = job->cacheKey;
            n->threeD = true;
            n->start = cameraStart;
            n->end = cameraEnd;
            n->transform.anchor = Vec3{0, 0, 0};
            n->transform.position = centroid;
            const f32 size = dist * sceneScale * 0.12f;
            if (kind == LayerKind::Shape) {
                n->shape.shapeType = 0;
                n->shape.bounds = Rect{0, 0, size, size};
                n->shape.fillColor = object == 4 ? Vec4{1, 1, 1, 1} : Vec4{0.15f, 0.75f, 1.0f, 1};
                n->shape.filled = true;
                n->transform.anchor = Vec3{size * 0.5f, size * 0.5f, 0};
            } else if (kind == LayerKind::Text) {
                n->text.content = "Text"; n->text.size = size;
            }
            if (plane) {
                // Eixo Y do nulo = para dentro do chão (Y do Aurea aponta para baixo).
                const auto calibratedC0 = res->sceneCalibration*Vec4{C0.x,C0.y,C0.z,1};
                if ((Vec3{calibratedC0.x,calibratedC0.y,calibratedC0.z} - centroid).dot(normal) < 0) normal = normal * -1.0f;
                const Vec3 planeAxis = normal * -1.0f;
                Vec3 xA = Vec3{1, 0, 0} - planeAxis * planeAxis.x;
                xA = xA.length() > 1e-4f ? xA.normalized() : Vec3{0, 0, 1};
                // Null's Y is the surface normal. A 2D shape/text occupies XY,
                // so its local Z (not Y) must be the surface normal.
                const Vec3 yA = kind == LayerKind::Null ? planeAxis : planeAxis.cross(xA);
                const Vec3 zA = xA.cross(yA);
                const f64 m[9] = {xA.x, yA.x, zA.x, xA.y, yA.y, zA.y, xA.z, yA.z, zA.z};
                n->transform.rotation = tracking::euler_zyx_from_matrix(m) * (1.0f / kDeg2Rad);
            }
        }
    }
    comp->rebuild_draw_order();
    project_->mark_dirty();
    request_render();
    (void)w;
    (void)h;
    return cid.pack();
}

namespace {
std::vector<Vec3> selected_camera_points(const CameraTrackResult& res,const std::vector<u32>& ids) {
    std::vector<Vec3> points;usize n=0;
    if(res.solution.rotationOnly)return points;
    for(u32 i=0;i<res.solution.trackSolved.size();++i)if(res.solution.trackSolved[i]){
        if(n>=res.solution.points.size())break;
        if(std::find(ids.begin(),ids.end(),i)!=ids.end())points.push_back(res.solution.points[n]);++n;
    }
    return points;
}
Mat4 camera_scene_mapping(const CameraTrackResult& res,const Composition& comp) {
    const auto def=scene3d::default_camera(comp.width(),comp.height());std::vector<f32> depths;
    for(const auto& p:res.solution.points)if(!res.solution.poses.empty()){
        const auto& pose=res.solution.poses.front();const f32 z=static_cast<f32>(pose.R[6]*p.x+pose.R[7]*p.y+pose.R[8]*p.z+pose.t[2]);if(z>0)depths.push_back(z);
    }
    f32 scale=1;if(!depths.empty()){std::nth_element(depths.begin(),depths.begin()+depths.size()/2,depths.end());scale=-def.position.z/std::max(1e-6f,depths[depths.size()/2]);}
    return res.sceneCalibration*Mat4::translation(def.position)*Mat4::scale(Vec3{scale,scale,scale});
}
Vec3 mapped_point(const Mat4& m,Vec3 p){auto q=m*Vec4{p.x,p.y,p.z,1};return {q.x,q.y,q.z};}
}

u32 Engine::camera_track_target(i64 frame,f32* xy,u32 capacity) noexcept {
    if(!cameraTrack_||cameraTrack_->state.load()!=2||!xy||capacity<32)return 0;
    std::shared_ptr<const CameraTrackResult> res;std::vector<u32> ids;
    {std::lock_guard<std::mutex>g(cameraTrack_->mutex);res=cameraTrack_->result;ids=cameraTrack_->selectedTracks;}
    if(!res)return 0;auto points=selected_camera_points(*res,ids);if(points.empty())return 0;
    std::lock_guard<std::mutex>g(modelMutex_);const auto* comp=project_&&projectSession_==cameraTrack_->session?current_composition():nullptr;
    const auto* video=comp&&project_->timeline().current()==cameraTrack_->composition?comp->layer(LayerId::unpack(cameraTrack_->layerId)):nullptr;
    const auto* asset=video?project_->asset(video->source):nullptr;const i64 local=video?frame-video->start.value:-1;
    if(!video||video->cameraTrack!=res||!asset||local<0||local>=res->frames)return 0;
    const auto& pose=res->solution.poses[static_cast<usize>(local)];if(!pose.valid)return 0;
    Vec3 center{},normal{};for(auto p:points)center+=p;center=center*(1.f/points.size());
    const f32 depth=static_cast<f32>(pose.R[6]*center.x+pose.R[7]*center.y+pose.R[8]*center.z+pose.t[2]);if(depth<=0)return 0;
    if(!tracking::dominant_plane(points,depth*.02f,.5f,center,normal))normal={static_cast<f32>(pose.R[6]),static_cast<f32>(pose.R[7]),static_cast<f32>(pose.R[8])};
    Vec3 x=Vec3{1,0,0}-normal*normal.x;if(x.length()<1e-4f)x=Vec3{0,0,1}-normal*normal.z;x=x.normalized();const auto y=normal.cross(x).normalized();
    const f32 focal=.5f*res->analysisH/std::tan(res->solution.fovY*.5f),radius=depth*24/focal;
    const auto matrix=layer_comp_matrix(*comp,*video,FrameIndex{frame});
    for(u32 i=0;i<32;++i){const f32 angle=static_cast<f32>(i)*6.283185307f/32;auto p=center+(x*std::cos(angle)+y*std::sin(angle))*radius;
        const f64 z=pose.R[6]*p.x+pose.R[7]*p.y+pose.R[8]*p.z+pose.t[2];if(z<=0)return 0;
        const f32 u=static_cast<f32>(focal*(pose.R[0]*p.x+pose.R[1]*p.y+pose.R[2]*p.z+pose.t[0])/z+res->analysisW*.5);
        const f32 v=static_cast<f32>(focal*(pose.R[3]*p.x+pose.R[4]*p.y+pose.R[5]*p.z+pose.t[1])/z+res->analysisH*.5);
        auto q=matrix*Vec4{u*asset->video.width/res->analysisW,v*asset->video.height/res->analysisH,0,1};if(q.w<=0)return 0;
        xy[i*2]=q.x/q.w;xy[i*2+1]=q.y/q.w;
    }return 32;
}

bool Engine::calibrate_camera_scene(u32 operation,f32 distance) noexcept {
    if(!cameraTrack_||!cameraTrack_->finished.load()||cameraTrack_->state.load()!=2||operation>2||!std::isfinite(distance)||distance<=0||distance>1e6)return false;
    auto* job=cameraTrack_.get();std::shared_ptr<const CameraTrackResult> res;std::vector<u32> ids;
    {std::lock_guard<std::mutex>g(job->mutex);res=job->result;ids=job->selectedTracks;}if(!res)return false;
    auto points=selected_camera_points(*res,ids);if(points.empty()||(operation==2&&points.size()!=2))return false;
    std::lock_guard<std::mutex>g(modelMutex_);auto* comp=project_&&projectSession_==job->session?current_composition():nullptr;
    auto* video=comp&&project_->timeline().current()==job->composition?comp->layer(LayerId::unpack(job->layerId)):nullptr;if(!video||video->cameraTrack!=res||res->sourceSignature!=tracking::source_signature(project_->asset(video->source))||!tracking::camera_timing_matches(*video,*res,comp->fps()))return false;
    std::vector<LayerId> roots;bool camera=false,externalParent=false;
    comp->layers().for_each([&](LayerId id,const Layer& l){
        if(l.cameraTrackSource!=LayerId::unpack(job->layerId)||l.cameraTrackKey!=job->cacheKey)return;
        camera|=l.kind==LayerKind::Camera;
        const auto* parent=comp->layer(l.parent);if(parent){if(parent->cameraTrackSource!=l.cameraTrackSource||parent->cameraTrackKey!=l.cameraTrackKey)externalParent=true;return;}
        roots.push_back(id);
    });
    if(!camera||externalParent)return false;
    const auto mapping=camera_scene_mapping(*res,*comp);Vec3 center{};for(auto& p:points){p=mapped_point(mapping,p);center+=p;}center=center*(1.f/points.size());
    Mat4 delta=Mat4::translation(-center);
    if(operation==1){
        Vec3 normal;if(!tracking::dominant_plane(points,std::max(1.f,-scene3d::default_camera(comp->width(),comp->height()).position.z*.02f),.6f,center,normal))return false;
        const auto cameraPosition=mapped_point(mapping,res->solution.poses.front().center());if((cameraPosition-center).dot(normal)<0)normal=normal*-1;
        const auto down=normal*-1;auto right=Vec3{1,0,0}-down*down.x;if(right.length()<1e-4f)right=Vec3{0,0,1}-down*down.z;right=right.normalized();const auto forward=right.cross(down);
        delta.col[0]={right.x,down.x,forward.x,0};delta.col[1]={right.y,down.y,forward.y,0};delta.col[2]={right.z,down.z,forward.z,0};delta.col[3]={-right.dot(center),-down.dot(center),-forward.dot(center),1};
    }else if(operation==2){const f32 measured=(points[1]-points[0]).length();if(measured<1e-5f)return false;const f32 scale=distance/measured;if(scale<1e-4f||scale>1e4f)return false;delta=Mat4::scale({scale,scale,scale});}
    history_.before_mutation(*comp,job->composition,operation==0?"Set tracked origin":operation==1?"Set tracked ground":"Set tracked distance");
    for(auto id:roots){auto* l=comp->layer(id);l->parentBasis=delta*(l->hasParentBasis?l->parentBasis:Mat4::identity());l->hasParentBasis=true;}
    auto next=std::make_shared<CameraTrackResult>(*res);next->sceneCalibration=delta*res->sceneCalibration;video=comp->layer(LayerId::unpack(job->layerId));video->cameraTrack=next;
    {std::lock_guard<std::mutex>jg(job->mutex);job->result=next;for(auto& p:job->appliedPoints)p=mapped_point(delta,p);}
    modelRevision_.fetch_add(1,std::memory_order_acq_rel);project_->mark_dirty();request_render();return true;
}

bool Engine::place_model_on_track(u64 id) noexcept {
    if(!cameraTrack_||cameraTrack_->state.load()!=2)return false;auto* job=cameraTrack_.get();
    std::shared_ptr<const CameraTrackResult>res;std::vector<u32>ids;{std::lock_guard<std::mutex>g(job->mutex);res=job->result;ids=job->selectedTracks;}if(!res)return false;
    auto points=selected_camera_points(*res,ids);if(points.empty())return false;
    std::lock_guard<std::mutex>g(modelMutex_);auto* comp=project_&&projectSession_==job->session?current_composition():nullptr;
    auto* layer=comp?comp->layer(LayerId::unpack(id)):nullptr;auto* source=comp?comp->layer(LayerId::unpack(job->layerId)):nullptr;
    if(!layer||layer->kind!=LayerKind::Model3D||!source||source->cameraTrack!=res||project_->timeline().current()!=job->composition)return false;
    if(res->sourceSignature!=tracking::source_signature(project_->asset(source->source))||!tracking::camera_timing_matches(*source,*res,comp->fps()))return false;
    bool camera=false;comp->layers().for_each([&](LayerId,const Layer& l){camera|=l.kind==LayerKind::Camera&&l.cameraTrackSource==LayerId::unpack(job->layerId)&&l.cameraTrackKey==job->cacheKey;});if(!camera)return false;
    const auto mapping=camera_scene_mapping(*res,*comp);Vec3 center{},normal{};for(auto& p:points){p=mapped_point(mapping,p);center+=p;}center=center*(1.f/points.size());
    const bool plane=tracking::dominant_plane(points,std::max(1.f,-scene3d::default_camera(comp->width(),comp->height()).position.z*.02f),.5f,center,normal);
    history_.before_mutation(*comp,job->composition,"Place model on tracked surface");layer->parent={};layer->parentBasis=Mat4::identity();layer->hasParentBasis=false;layer->threeD=true;layer->transform.position=center;layer->transform.rotation={};layer->start=source->start;layer->end=source->end;
    for(auto property:{TrackProperty::PositionX,TrackProperty::PositionY,TrackProperty::PositionZ,TrackProperty::RotationX,TrackProperty::RotationY,TrackProperty::RotationZ})if(auto* tr=layer->tracks.find(property))tr->clear();
    if(plane){const auto cp=mapped_point(mapping,res->solution.poses.front().center());if((cp-center).dot(normal)<0)normal=normal*-1;const auto down=normal*-1;auto right=Vec3{1,0,0}-down*down.x;if(right.length()<1e-4f)right=Vec3{0,0,1}-down*down.z;right=right.normalized();const auto forward=right.cross(down);const f64 m[]={right.x,down.x,forward.x,right.y,down.y,forward.y,right.z,down.z,forward.z};layer->transform.rotation=tracking::euler_zyx_from_matrix(m)*(1.f/kDeg2Rad);}
    layer->cameraTrackSource=LayerId::unpack(job->layerId);layer->cameraTrackKey=job->cacheKey;modelRevision_.fetch_add(1,std::memory_order_acq_rel);project_->mark_dirty();request_render();return true;
}

u32 Engine::select_camera_track_points(const u32* ids, u32 count, u32 operation) noexcept {
    if (!cameraTrack_ || cameraTrack_->state.load() != 2 || (count && !ids) || count > 1500 || operation > 2) return 0;
    std::lock_guard<std::mutex> lock(cameraTrack_->mutex);
    const auto res = cameraTrack_->result;
    if (!res) return 0;
    auto& selected = cameraTrack_->selectedTracks;
    if (operation == 0) selected.clear();
    for (u32 i = 0; i < count; ++i) {
        const u32 id = ids[i];
        if (id >= res->solution.trackSolved.size() || !res->solution.trackSolved[id] || res->solution.rotationOnly) continue;
        const auto found = std::find(selected.begin(), selected.end(), id);
        if (operation == 2) { if (found != selected.end()) selected.erase(found); }
        else if (found == selected.end()) selected.push_back(id);
    }
    return static_cast<u32>(selected.size());
}

u32 Engine::camera_track_features_detail(i64 frame, f32* out, u32 maxPoints) noexcept {
    if (!cameraTrack_ || !out || !maxPoints) return 0;
    auto* job = cameraTrack_.get();
    std::shared_ptr<const CameraTrackResult> res;
    std::vector<u32> selected;
    { std::lock_guard<std::mutex> lock(job->mutex); res = job->result; selected = job->selectedTracks; }
    if (!res || !res->analysisW || !res->analysisH) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ && projectSession_ == job->session && project_->timeline().current() == job->composition ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(job->layerId)) : nullptr;
    const Asset* a = l ? project_->asset(l->source) : nullptr;
    if (!a || l->source != job->source || l->cameraTrack != res || res->sourceSignature!=tracking::source_signature(a)) return 0;
    const i64 index = frame - l->start.value;
    if (index < 0 || index >= res->frames) return 0;
    const i64 sourceUs = static_cast<i64>(std::llround(std::max(0.0, l->source_frame(FrameIndex{frame})) * 1e6 / comp->fps()));
    if (sourceUs != res->sourceUs[static_cast<usize>(index)]) return 0;
    const auto& s = res->solution;
    const f64 focal = 0.5 * res->analysisH / std::tan(s.fovY * 0.5);
    const auto* pose = index < static_cast<i64>(s.poses.size()) && s.poses[static_cast<usize>(index)].valid ? &s.poses[static_cast<usize>(index)] : nullptr;
    const Mat4 m = layer_comp_matrix(*comp, *l, FrameIndex{frame});
    const f32 kx = static_cast<f32>(a->video.width) / res->analysisW, ky = static_cast<f32>(a->video.height) / res->analysisH;
    usize point = 0; u32 n = 0;
    for (usize t = 0; t < res->tracks.pos.size() && n < maxPoints; ++t) {
        const bool solved = t < s.trackSolved.size() && s.trackSolved[t] && !s.rotationOnly;
        const usize pindex = point;
        if (solved) ++point;
        const Vec2 observed = res->tracks.pos[t][static_cast<usize>(index)];
        if (!tracking::Tracks2D::present(observed)) continue;
        Vec2 pixel = observed; f32 error = -1, confidence = 0;
        if (solved && pose && pindex < s.points.size()) {
            const Vec3 p = s.points[pindex];
            const f64 x = pose->R[0]*p.x + pose->R[1]*p.y + pose->R[2]*p.z + pose->t[0];
            const f64 y = pose->R[3]*p.x + pose->R[4]*p.y + pose->R[5]*p.z + pose->t[1];
            const f64 z = pose->R[6]*p.x + pose->R[7]*p.y + pose->R[8]*p.z + pose->t[2];
            if (z <= 0) continue;
            pixel = Vec2{static_cast<f32>(focal*x/z + res->analysisW*0.5), static_cast<f32>(focal*y/z + res->analysisH*0.5)};
            error = std::hypot(pixel.x-observed.x, pixel.y-observed.y);
            confidence = 1.0f / (1.0f + error);
        }
        const Vec4 q = m * Vec4{pixel.x*kx, pixel.y*ky, 0, 1};
        if (q.w <= 1e-6f) continue;
        out[n*6] = q.x/q.w; out[n*6+1] = q.y/q.w; out[n*6+2] = confidence;
        out[n*6+3] = static_cast<f32>(t);
        out[n*6+4] = std::find(selected.begin(), selected.end(), t) != selected.end() ? 1.0f : 0.0f;
        out[n*6+5] = error; ++n;
    }
    return n;
}

u32 Engine::camera_track_features(i64 frame, f32* out, u32 maxPoints) noexcept {
    if (!cameraTrack_ || !out || maxPoints == 0) return 0;
    CameraTrackJob* job = cameraTrack_.get();
    std::shared_ptr<const CameraTrackResult> res;
    {
        std::lock_guard<std::mutex> g(job->mutex);
        res = job->result;
    }
    if (!res || res->tracks.width == 0) return 0;
    const i64 i = frame - job->start;
    if (i < 0 || i >= static_cast<i64>(res->tracks.frames)) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(job->layerId)) : nullptr;
    const Asset* a = l ? project_->asset(l->source) : nullptr;
    if (!l || !a || a->video.width == 0) return 0;
    // px da análise → px do vídeo → composição (transform da camada no quadro).
    const f32 kx = static_cast<f32>(a->video.width) / static_cast<f32>(res->tracks.width);
    const f32 ky = static_cast<f32>(a->video.height) / static_cast<f32>(res->tracks.height);
    const Mat4 m = layer_comp_matrix(*comp, *l, FrameIndex{frame});
    u32 n = 0;
    for (usize t = 0; t < res->tracks.pos.size() && n < maxPoints; ++t) {
        const Vec2 p = res->tracks.pos[t][static_cast<usize>(i)];
        if (!tracking::Tracks2D::present(p)) continue;
        const Vec4 c = m * Vec4{p.x * kx, p.y * ky, 0, 1};
        if (c.w <= 1e-6f) continue;
        out[n * 3] = c.x / c.w;
        out[n * 3 + 1] = c.y / c.w;
        out[n * 3 + 2] = t < res->solution.trackSolved.size() && res->solution.trackSolved[t] ? 1.0f : 0.0f;
        ++n;
    }
    return n;
}

std::vector<Vec3> Engine::camera_track_points() noexcept {
    return cameraTrack_ ? cameraTrack_->appliedPoints : std::vector<Vec3>{};
}

bool Engine::set_echo(u64 layerId, u32 count, f32 delay, f32 decay) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "eco");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->echoCount = std::min<u32>(count, 16u);
    l->echoDelay = std::clamp(delay, 0.25f, 120.0f);
    l->echoDecay = std::clamp(decay, 0.0f, 1.0f);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_rgb_time(u64 layerId, f32 delay) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "rgb no tempo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->rgbDelay = std::clamp(delay, 0.0f, 60.0f);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_echo(u64 layerId, f32* out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !out) return false;
    out[0] = static_cast<f32>(l->echoCount);
    out[1] = l->echoDelay;
    out[2] = l->echoDecay;
    out[3] = l->rgbDelay;
    return true;
}

bool Engine::set_transition(u64 layerId, bool out, u32 type, u32 frames) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || type > 5) return false;
    history_.before_mutation(*comp, project_->timeline().current(), out ? "transicao de saida" : "transicao de entrada");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const u32 maxF = static_cast<u32>(std::max<i64>(1, (l->end.value - l->start.value) / 2));
    const u32 f = std::clamp<u32>(frames, 1u, maxF);
    if (out) { l->transitionOut = static_cast<u8>(type); l->transitionOutFrames = type ? f : 0; }
    else { l->transitionIn = static_cast<u8>(type); l->transitionInFrames = type ? f : 0; }
    project_->mark_dirty();
    request_render();
    return true;
}

namespace {

/// Os presets do AUREA PARTICULAR.
///
/// Os tres primeiros sao os nomes que o app tinha como SISTEMAS SEPARADOS
/// (Faiscas, Neve, Poeira de luz): viraram preset do mesmo sistema, que e o que
/// eles sempre foram por dentro. Os outros sete sao do sistema novo.
///
/// Um preset e so um ponto de partida — todos os parametros continuam
/// editaveis depois, e nenhum deles cria um motor proprio.
void particle_preset(ParticleData& p, u32 preset, f32 w, f32 h) noexcept {
    p = ParticleData{};
    if (preset >= 10 && preset <= 18) {
        // Particle World: new analytic 3D dynamics, compact mobile controls.
        // IDs 0..9 remain readable for existing projects, but are no longer
        // offered by the mobile creation/preset panels.
        p.emitterType = static_cast<u32>(ParticleEmitter::WorldExplosive) + std::min(preset, 13u) - 10;
        p.emitterRadius = w * .025f;
        p.rate = 1000; p.lifetime = 1; p.lifeRandom = 0;
        p.speed = w * .25f; p.speedRandom = .5f;
        p.gravity = Vec3{0, -w * .25f, 0};
        p.spread = preset == 11 ? 30.0f : 360.0f;
        p.direction = -90; p.emitterSpace = 1;
        p.startSize = w / 960; p.endSize = w / 960;
        p.startOpacity = .75f; p.endOpacity = 0;
        p.startColor = Vec4{1, 1, .3137255f, 1};
        p.endColor = Vec4{.7843137f, .1568627f, .1568627f, 1};
        p.particleType = static_cast<u32>(ParticleShape::Streak);
        p.trailLength = .035f; p.trailTaper = 0;
        p.sizeRandom = .5f; p.softness = .1f;
        p.maxParticles = 12000; p.blendMode = 1;
        if (preset == 12) { p.gravity = Vec3{}; p.lifetime = 2; p.speed *= .4f; }
        if (preset == 13) {
            p.emitterSize = Vec2{w * 1.82f, h * 1.82f}; p.emitterDepth = w * .13f;
            p.rate = 30; p.lifetime = 5; p.speed = w * .012f;
            p.gravity = Vec3{0, -w * .021f, 0};
            p.startSize = w * .014f; p.endSize = w * .024f;
            p.startColor = Vec4{.15f,1,.35f,1}; p.endColor = Vec4{.05f,.3f,1,1};
            p.particleType = static_cast<u32>(ParticleShape::Soft); p.softness = .9f;
            p.startOpacity = .65f; p.trailLength = 0;
            p.auxCount = 8; p.auxLife = 1; p.auxSpeed = w * .004f; p.auxSize = w * .003f;
            p.auxColor = Vec4{.1f,.8f,1,.5f};
        }
        if (preset >= 14) {
            p.emitterType = static_cast<u32>(ParticleEmitter::WorldBox);
            p.emitterSize = Vec2{w * 1.2f, h * 1.2f}; p.emitterDepth = w * .5f;
            p.rate = 70; p.lifetime = 5; p.gravity = Vec3{}; p.speed = w * .015f;
            p.startSize = w * .003f; p.endSize = w * .004f; p.trailLength = 0;
            p.particleType = static_cast<u32>(ParticleShape::Soft); p.softness = .8f;
            p.startColor = Vec4{1, .7f, .2f, 1}; p.endColor = Vec4{1, .2f, .04f, 0};
            if (preset == 14) { // Embers rising through a volume.
                p.gravity.y = -w * .012f; p.emitterDepth = w * .2f;
            } else if (preset == 15) { // Snow, without additive overexposure.
                p.gravity.y = w * .01f; p.rate = 100; p.blendMode = 0;
                p.startColor = Vec4{1,1,1,1}; p.endColor = Vec4{.8f,.9f,1,0};
            } else if (preset == 16) { // Defocused colored lights.
                p.rate = 18; p.startSize = w * .018f; p.endSize = w * .03f;
                p.startOpacity = .35f; p.startColor = Vec4{.4f,.3f,1,1}; p.endColor = Vec4{1,.2f,.6f,0};
            } else if (preset == 18) { // Floating violet shards, deterministic in either seek direction.
                p.rate = 28; p.lifetime = 7; p.speed = w * .009f;
                p.emitterDepth = w * .8f; p.maxParticles = 1500;
                p.startSize = w * .026f; p.endSize = w * .018f; p.sizeRandom = .8f;
                p.startColor = Vec4{1,.18f,1,1}; p.endColor = Vec4{.4f,.015f,.7f,0};
                p.startOpacity = 1; p.rotationRandom = 180; p.spin = 35;
                p.particleType = static_cast<u32>(ParticleShape::Crystal); p.softness = .12f;
            } else { // Fast narrow fountain.
                p.emitterType = static_cast<u32>(ParticleEmitter::WorldJet);
                p.emitterRadius = w * .008f; p.rate = 350; p.lifetime = 1.8f;
                p.speed = w * .4f; p.spread = 18; p.gravity.y = w * .12f;
                p.particleType = static_cast<u32>(ParticleShape::Streak); p.trailLength = .025f;
            }
        }
        return;
    }
    switch (preset) {
        case 1:   // Neve: cai devagar, de toda a largura do topo
            p.rate = 40; p.lifetime = 9; p.speed = 70; p.spread = 25; p.gravity = Vec3{0, 0, 0};
            p.startSize = 9; p.endSize = 9; p.startOpacity = 0.9f; p.endOpacity = 0.5f;
            p.startColor = Vec4{1, 1, 1, 1}; p.endColor = Vec4{0.85f, 0.92f, 1, 1};
            p.direction = 90; p.blendMode = 0;
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, 10}; p.emitterOffset = Vec2{0, -h * 0.5f - 10};
            p.wind = Vec3{18, 0, 0};   // deriva lateral: neve nao cai reta
            p.particleType = static_cast<u32>(ParticleShape::Soft);
            break;
        case 2:   // Poeira de luz: sobe devagar, grande e suave
            p.rate = 12; p.lifetime = 6; p.speed = 25; p.spread = 360; p.gravity = Vec3{0, 0, 0};
            p.startSize = 26; p.endSize = 44; p.startOpacity = 0.55f; p.endOpacity = 0;
            p.startColor = Vec4{1, 0.92f, 0.75f, 1}; p.endColor = Vec4{1, 0.8f, 0.55f, 1};
            p.direction = -90; p.blendMode = 1;
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, h};
            p.turbulence = 12; p.turbulenceScale = 0.6f;
            p.particleType = static_cast<u32>(ParticleShape::Soft);
            p.softness = 0.8f;
            break;
        case 3:   // Chuva: rapida, fina, com rastro
            p.rate = 420; p.lifetime = 1.1f; p.speed = 1400; p.spread = 3; p.direction = 92;
            p.gravity = Vec3{0, -600, 0}; p.startSize = 2; p.endSize = 2;
            p.startOpacity = 0.75f; p.endOpacity = 0.25f;
            p.startColor = Vec4{0.72f, 0.82f, 1, 1}; p.endColor = Vec4{0.6f, 0.75f, 1, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, 8}; p.emitterOffset = Vec2{0, -h * 0.5f - 8};
            p.particleType = static_cast<u32>(ParticleShape::Streak);
            p.trailLength = 0.045f; p.maxParticles = 24000;
            break;
        case 4:   // Vaga-lumes: poucos, lentos, piscando pela vida
            p.rate = 9; p.lifetime = 7; p.speed = 34; p.spread = 360; p.gravity = Vec3{0, 0, 0};
            p.startSize = 7; p.endSize = 3; p.startOpacity = 0.9f; p.endOpacity = 0.15f;
            p.startColor = Vec4{1, 0.95f, 0.45f, 1}; p.endColor = Vec4{0.75f, 1, 0.4f, 1};
            p.direction = -90; p.blendMode = 1;
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, h};
            p.turbulence = 26; p.turbulenceScale = 0.45f;
            p.attractor = -4;   // afasta devagar do centro
            p.particleType = static_cast<u32>(ParticleShape::Soft);
            p.softness = 0.9f;
            break;
        case 5:   // Brasas: sobem, esfriam e apagam
            p.rate = 90; p.lifetime = 2.6f; p.speed = 210; p.spread = 40; p.direction = -92;
            p.gravity = Vec3{0, -60, 0}; p.startSize = 8; p.endSize = 2;
            p.startOpacity = 1; p.endOpacity = 0;
            p.startColor = Vec4{1, 0.78f, 0.30f, 1}; p.endColor = Vec4{0.85f, 0.18f, 0.05f, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Sphere);
            p.emitterRadius = 26; p.emitterOffset = Vec2{0, h * 0.30f};
            p.turbulence = 40; p.turbulenceScale = 1.4f;
            p.rotationRandom = 180; p.spin = 90;
            p.auxCount = 2; p.auxAt = 0.45f; p.auxLife = 0.55f; p.auxSpeed = 130;
            p.auxSize = 3; p.auxSpread = 200; p.auxColor = Vec4{1, 0.55f, 0.15f, 1};
            break;
        case 6:   // Confete: estoura e cai girando, quicando no chao
            p.rate = 0; p.burst = 220; p.lifetime = 3.4f; p.speed = 620; p.spread = 360;
            p.direction = -90; p.gravity = Vec3{0, -900, 0}; p.drag = 0.9f;
            p.startSize = 13; p.endSize = 13; p.startOpacity = 1; p.endOpacity = 0.9f;
            p.startColor = Vec4{1, 0.30f, 0.45f, 1}; p.endColor = Vec4{0.35f, 0.75f, 1, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Point);
            p.emitterOffset = Vec2{0, -h * 0.12f};
            p.particleType = static_cast<u32>(ParticleShape::Square);
            p.rotationRandom = 180; p.spin = 420;
            p.collision = static_cast<u32>(ParticleCollision::Plane);
            p.collisionY = h * 0.45f; p.collisionBounce = 0.25f;
            break;
        case 7:   // Campo de estrelas: pontos distantes, quase parados
            p.rate = 60; p.lifetime = 13; p.speed = 4; p.spread = 360; p.gravity = Vec3{0, 0, 0};
            p.startSize = 3; p.endSize = 2; p.startOpacity = 0.55f; p.endOpacity = 0.9f;
            p.startColor = Vec4{0.85f, 0.9f, 1, 1}; p.endColor = Vec4{1, 1, 1, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, h};
            p.particleType = static_cast<u32>(ParticleShape::Soft);
            p.softness = 0.7f;
            break;
        case 8:   // Poeira magica: espiral fechada subindo
            p.rate = 70; p.lifetime = 4.2f; p.speed = 120; p.spread = 20; p.direction = -90;
            p.gravity = Vec3{0, 0, 0}; p.drag = 0.5f;
            p.startSize = 10; p.endSize = 1; p.startOpacity = 0.95f; p.endOpacity = 0;
            p.startColor = Vec4{0.75f, 0.55f, 1, 1}; p.endColor = Vec4{0.35f, 0.85f, 1, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Disc);
            p.emitterRadius = 60;
            p.vortex = 220; p.attractor = 6;
            p.turbulence = 20; p.turbulenceScale = 1.1f;
            p.auxCount = 3; p.auxAt = 0.3f; p.auxLife = 0.7f; p.auxSpeed = 60;
            p.auxSize = 4; p.auxSpread = 360; p.auxColor = Vec4{0.8f, 0.6f, 1, 1};
            break;
        case 9:   // Explosao de logo: estoura para fora segurando o rastro
            p.rate = 0; p.burst = 320; p.lifetime = 1.8f; p.speed = 420; p.spread = 360;
            p.direction = -90; p.gravity = Vec3{0, 0, 0}; p.drag = 3.2f;
            p.startSize = 9; p.endSize = 1; p.startOpacity = 1; p.endOpacity = 0;
            p.startColor = Vec4{1, 0.85f, 0.4f, 1}; p.endColor = Vec4{1, 0.25f, 0.1f, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Point);
            p.particleType = static_cast<u32>(ParticleShape::Streak);
            p.trailLength = 0.09f; p.trailTaper = 1;
            p.auxCount = 2; p.auxAt = 0.5f; p.auxLife = 0.4f; p.auxSpeed = 200;
            p.auxSize = 3; p.auxSpread = 360; p.auxColor = Vec4{1, 0.6f, 0.2f, 1};
            break;
        default:  // 0 — Faiscas: jato para cima com gravidade
            p.rate = 120; p.lifetime = 1.3f; p.speed = 480; p.spread = 50; p.gravity = Vec3{0, -900, 0};
            p.startSize = 10; p.endSize = 2; p.startOpacity = 1; p.endOpacity = 0;
            p.direction = -90; p.blendMode = 1;
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{24, 24};
            p.particleType = static_cast<u32>(ParticleShape::Streak);
            p.trailLength = 0.06f;
            p.turbulence = 30; p.turbulenceScale = 1.2f;
            break;
    }
}

/// Preset 9 (Explosão de logo): com uma camada de TEXTO na composição, o logo
/// é o texto — o estouro sai dos glifos (emissor Texto, superfície), a de
/// cima na pilha. Sem texto, fica o ponto de sempre.
void logo_burst_from_text(const Composition& comp, LayerId self, ParticleData& p) noexcept {
    const OrderedIds<LayerId>& order = comp.order();
    for (u32 i = order.size(); i-- > 0;) {
        const LayerId id = order.at(i);
        const Layer* t = comp.layer(id);
        if (!t || id == self || t->kind != LayerKind::Text || !t->visible || t->text.content.empty()) continue;
        p.emitterType = static_cast<u32>(ParticleEmitter::Text);
        p.emitterSource = id.pack();
        p.emitFrom = 1;
        return;
    }
}

/// Particular (o sistema de partículas do app antigo): a camada vira folha
/// transparente (ParticleEmitter::Particular) e o efeito, o PRIMEIRO da
/// pilha, desenha as partículas com o preset `preset` (0..kPresetCount-1).
/// Numa camada antiga, é a conversão: o motor antigo sai, o Particular entra.
bool make_particular(Layer& l, const EffectRegistry& registry, u32 preset) noexcept {
    const EffectTypeId type = registry.find_key(effect_keys::kParticular);
    const ParameterRegistry* params = registry.params(type);
    if (!params || preset >= particular::kPresetCount) return false;
    auto it = std::find_if(l.effects.begin(), l.effects.end(), [&](const EffectInstance& e) { return e.type == type; });
    if (it == l.effects.end()) {
        if (l.effects.size() >= kMaxEffectCount) return false;
        EffectInstance e;
        e.id = l.alloc_effect_id();
        e.type = type;
        initialize_instance(e, *params);
        e.params[particular::kSeed].constant = ParamValue::scalar(static_cast<f32>(l.particles.seed % 10000u));
        it = l.effects.insert(l.effects.begin(), std::move(e));
    } else {
        // Preset é ponto de partida: os keyframes do efeito saem com ele.
        const u32 id = it->id;
        l.tracks.remove_if([id](const Track& t) { return t.property == TrackProperty::EffectParam && t.effectIndex == id; });
    }
    l.particles.emitterType = static_cast<u32>(ParticleEmitter::Particular);
    l.tracks.remove_if([](const Track& t) { return t.property == TrackProperty::ParticleParam; });
    return particular::apply_preset(*it, *params, preset);
}

} // namespace

Result<u64> Engine::add_particles(u32 preset) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    history_.before_mutation(*comp, project_->timeline().current(), "adicionar particulas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    // UM sistema, UM nome. Os tres nomes antigos viraram preset: criar uma
    // camada chamada "Neve" sugeria um motor de neve, e nao existe motor de
    // neve — e o Particular com os parametros da neve.
    // Presets `particular::kPresetBase + i`: o Particular (o sistema do app
    // antigo), que é o que a interface cria. 0..18: o motor antigo, mantido
    // só para projetos e testes que ainda pedem esses números.
    const bool particularPreset = preset >= particular::kPresetBase
                               && preset < particular::kPresetBase + particular::kPresetCount;
    const LayerId lid = comp->add_layer(LayerKind::ParticleSystem,
                                        particularPreset ? "Particular" : preset >= 10 ? "Particle World" : "Aurea Particular");
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    const f32 w = static_cast<f32>(comp->width()), h = static_cast<f32>(comp->height());
    l->particles.seed = lid.index * 7919u + 1u;
    if (particularPreset) {
        if (!make_particular(*l, effectRegistry_, preset - particular::kPresetBase)) {
            (void)comp->remove_layer(lid);
            return Status{Errc::InvalidState, "particular indisponivel"};
        }
    } else {
        particle_preset(l->particles, preset, w, h);
        if (preset >= 10 && preset <= 18) l->threeD = true;
        if (preset == 9) logo_burst_from_text(*comp, lid, l->particles);
        l->particles.seed = lid.index * 7919u + 1u;
    }
    // O cursor pode estar DEPOIS do fim da composição (a timeline não trava
    // mais no fim): a camada nasce onde ele está e a duração acompanha, senão
    // ela nasceria fora do projeto e não apareceria.
    const i64 t = std::max<i64>(0, playback_.current().value);
    l->start = FrameIndex{t};
    l->end = FrameIndex{std::max<i64>(t + 1, comp->duration().value)};
    if (l->end.value > comp->duration().value) {
        comp->set_duration(l->end);
        playback_.configure(comp->fps(), comp->duration());
    }
    l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0};
    l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0};
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

bool Engine::apply_particle_preset(u64 layerId, u32 preset) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::ParticleSystem) return false;
    if (preset >= particular::kPresetBase && preset < particular::kPresetBase + particular::kPresetCount) {
        // Particular: numa camada antiga é a conversão para o sistema novo
        // (a camada fica 2D — a perspectiva é a do próprio Particular).
        history_.before_mutation(*comp, project_->timeline().current(), "preset de particulas");
        modelRevision_.fetch_add(1, std::memory_order_acq_rel);
        if (l->particles.emitterType != static_cast<u32>(ParticleEmitter::Particular)) l->threeD = false;
        const bool ok = make_particular(*l, effectRegistry_, preset - particular::kPresetBase);
        project_->mark_dirty();
        request_render();
        return ok;
    }
    history_.before_mutation(*comp, project_->timeline().current(), "preset de particulas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const u32 seed = l->particles.seed;
    particle_preset(l->particles, preset, static_cast<f32>(comp->width()), static_cast<f32>(comp->height()));
    if (preset >= 10 && preset <= 18) {
        l->threeD = true;
        l->tracks.remove_if([](const Track& track) { return track.property == TrackProperty::ParticleParam; });
    }
    if (preset == 9) logo_burst_from_text(*comp, LayerId::unpack(layerId), l->particles);
    l->particles.seed = seed;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_particle_param(u64 layerId, u32 param, f32 v) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::ParticleSystem || param >= static_cast<u32>(ParticleParam::Count)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "particulas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    ParticleData& p = l->particles;
    // O INDICE e contrato com a UI (ver ParticleParam em Layer.hpp): nao
    // reordene sem renumerar os dois lados. Cada faixa de clamp existe para um
    // valor absurdo nao virar NaN no shader — que nao desenha nada e nao diz
    // por que.
    switch (static_cast<ParticleParam>(param)) {
        // --- Emissor ---------------------------------------------------------
        case ParticleParam::EmitterType:
            p.emitterType = static_cast<u32>(std::clamp(v, 0.0f, static_cast<f32>(ParticleEmitter::Count) - 1.0f));
            break;
        case ParticleParam::EmitterWidth:  p.emitterSize.x = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::EmitterHeight: p.emitterSize.y = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::EmitterRadius: p.emitterRadius = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::EmitterRotation: p.emitterRotation = v; break;
        case ParticleParam::EmitterDepth: p.emitterDepth = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::GridX: p.gridX = static_cast<u32>(std::clamp(v, 1.0f, 64.0f)); break;
        case ParticleParam::GridY: p.gridY = static_cast<u32>(std::clamp(v, 1.0f, 64.0f)); break;
        case ParticleParam::EmitFill: p.emitFill = v >= 0.5f; break;
        case ParticleParam::EmitterOffsetX: p.emitterOffset.x = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::EmitterOffsetY: p.emitterOffset.y = std::clamp(v, -20000.0f, 20000.0f); break;

        // --- Emissao ---------------------------------------------------------
        case ParticleParam::Rate: p.rate = std::clamp(v, 0.0f, 1000000.0f); break;
        case ParticleParam::Burst: p.burst = static_cast<u32>(std::clamp(v, 0.0f, 1000000.0f)); break;
        case ParticleParam::Lifetime: p.lifetime = std::clamp(v, 0.05f, 120.0f); break;
        case ParticleParam::LifeRandom: p.lifeRandom = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::Speed: p.speed = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::SpeedRandom: p.speedRandom = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::Direction: p.direction = v; break;
        case ParticleParam::Spread: p.spread = std::clamp(v, 0.0f, 360.0f); break;
        case ParticleParam::InheritVelocity: p.inheritVelocity = std::clamp(v, 0.0f, 4.0f); break;
        case ParticleParam::Seed: p.seed = static_cast<u32>(std::clamp(v, 0.0f, 1000000.0f)); break;

        // --- Particula -------------------------------------------------------
        case ParticleParam::ParticleType:
            p.particleType = static_cast<u32>(std::clamp(v, 0.0f, static_cast<f32>(ParticleShape::Count) - 1.0f));
            break;
        case ParticleParam::Softness: p.softness = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::Rotation: p.rotation = v; break;
        case ParticleParam::RotationRandom: p.rotationRandom = std::clamp(v, 0.0f, 360.0f); break;
        case ParticleParam::Spin: p.spin = std::clamp(v, -3600.0f, 3600.0f); break;

        // --- Ao longo da vida ------------------------------------------------
        case ParticleParam::StartSize: p.startSize = std::clamp(v, 0.0f, 4000.0f); break;
        case ParticleParam::EndSize: p.endSize = std::clamp(v, 0.0f, 4000.0f); break;
        case ParticleParam::StartOpacity: p.startOpacity = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::EndOpacity: p.endOpacity = std::clamp(v, 0.0f, 1.0f); break;

        // --- Fisica ----------------------------------------------------------
        case ParticleParam::GravityX: p.gravity.x = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::GravityY: p.gravity.y = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::GravityZ: p.gravity.z = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::Drag: p.drag = std::clamp(v, 0.0f, 20.0f); break;
        case ParticleParam::WindX: p.wind.x = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::WindY: p.wind.y = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::Turbulence: p.turbulence = std::clamp(v, 0.0f, 5000.0f); break;
        case ParticleParam::TurbulenceScale: p.turbulenceScale = std::clamp(v, 0.05f, 20.0f); break;
        case ParticleParam::TurbulenceSpeed: p.turbulenceSpeed = std::clamp(v, 0.0f, 20.0f); break;
        case ParticleParam::Vortex: p.vortex = std::clamp(v, -3600.0f, 3600.0f); break;
        case ParticleParam::Attractor: p.attractor = std::clamp(v, -200.0f, 200.0f); break;

        // --- Rastro ----------------------------------------------------------
        case ParticleParam::TrailLength: p.trailLength = std::clamp(v, 0.0f, 2.0f); break;
        case ParticleParam::TrailTaper: p.trailTaper = std::clamp(v, 0.0f, 1.0f); break;

        // --- Aux -------------------------------------------------------------
        case ParticleParam::AuxCount: p.auxCount = static_cast<u32>(std::clamp(v, 0.0f, 16.0f)); break;
        case ParticleParam::AuxAt: p.auxAt = std::clamp(v, 0.0f, 0.99f); break;
        case ParticleParam::AuxLife: p.auxLife = std::clamp(v, 0.05f, 20.0f); break;
        case ParticleParam::AuxSpeed: p.auxSpeed = std::clamp(v, 0.0f, 10000.0f); break;
        case ParticleParam::AuxSize: p.auxSize = std::clamp(v, 0.0f, 500.0f); break;
        case ParticleParam::AuxSpread: p.auxSpread = std::clamp(v, 0.0f, 360.0f); break;

        // --- Colisao ---------------------------------------------------------
        case ParticleParam::Collision:
            p.collision = static_cast<u32>(std::clamp(v, 0.0f, static_cast<f32>(ParticleCollision::Count) - 1.0f));
            break;
        case ParticleParam::CollisionY: p.collisionY = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::CollisionBounce: p.collisionBounce = std::clamp(v, 0.0f, 1.0f); break;

        // --- Render ----------------------------------------------------------
        case ParticleParam::BlendMode: p.blendMode = static_cast<u32>(std::clamp(v, 0.0f, 1.0f)); break;
        case ParticleParam::MaxParticles: p.maxParticles = static_cast<u32>(std::clamp(v, 1.0f, 1000000.0f)); break;

        // --- 8.2 (v21) -------------------------------------------------------
        case ParticleParam::EmitterSpace: p.emitterSpace = v >= 0.5f ? 1u : 0u; break;
        case ParticleParam::EmitFrom: p.emitFrom = static_cast<u32>(std::clamp(v, 0.0f, 2.0f)); break;
        case ParticleParam::AuxProbability: p.auxProbability = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::TrailWidth: p.trailWidth = std::clamp(v, 0.0f, 8.0f); break;
        case ParticleParam::TrailOpacity: p.trailOpacity = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::SizeRandom: p.sizeRandom = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::OpacityRandom: p.opacityRandom = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::ColorRandom: p.colorRandom = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::CollisionX: p.collisionCenter.x = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::CollisionZ: p.collisionCenter.z = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::CollisionRadius: p.collisionRadius = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::CollisionWidth: p.collisionBox.x = std::clamp(v, 0.0f, 40000.0f); break;
        case ParticleParam::CollisionHeight: p.collisionBox.y = std::clamp(v, 0.0f, 40000.0f); break;
        case ParticleParam::CollisionDepth: p.collisionBox.z = std::clamp(v, 0.0f, 40000.0f); break;
        case ParticleParam::MeshScale: p.meshScale = std::clamp(v, 0.001f, 1000.0f); break;
        case ParticleParam::MeshLit: p.meshLit = v >= 0.5f; break;
        default: return false;
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_particles(u64 layerId, f32* out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::ParticleSystem || !out) return false;
    // UM valor por ParticleParam, na MESMA ordem do enum: a UI le pelo indice,
    // entao a ordem aqui e o outro lado do contrato de `set_particle_param`.
    // Com keyframe, sai o valor do instante: o controle da UI mostra o que a
    // previa esta desenhando.
    const ParticleData p = sampled_particles(*l, l->local_time(playback_.current()));
    out[0]  = static_cast<f32>(p.emitterType);
    out[1]  = p.emitterSize.x;
    out[2]  = p.emitterSize.y;
    out[3]  = p.emitterRadius;
    out[4]  = p.emitterRotation;
    out[5]  = p.emitterDepth;
    out[6]  = static_cast<f32>(p.gridX);
    out[7]  = static_cast<f32>(p.gridY);
    out[8]  = p.emitFill ? 1.0f : 0.0f;
    out[9]  = p.emitterOffset.x;
    out[10] = p.emitterOffset.y;
    out[11] = p.rate;
    out[12] = static_cast<f32>(p.burst);
    out[13] = p.lifetime;
    out[14] = p.lifeRandom;
    out[15] = p.speed;
    out[16] = p.speedRandom;
    out[17] = p.direction;
    out[18] = p.spread;
    out[19] = p.inheritVelocity;
    out[20] = static_cast<f32>(p.seed);
    out[21] = static_cast<f32>(p.particleType);
    out[22] = p.softness;
    out[23] = p.rotation;
    out[24] = p.rotationRandom;
    out[25] = p.spin;
    out[26] = p.startSize;
    out[27] = p.endSize;
    out[28] = p.startOpacity;
    out[29] = p.endOpacity;
    out[30] = p.gravity.x;
    out[31] = p.gravity.y;
    out[32] = p.gravity.z;
    out[33] = p.drag;
    out[34] = p.wind.x;
    out[35] = p.wind.y;
    out[36] = p.turbulence;
    out[37] = p.turbulenceScale;
    out[38] = p.turbulenceSpeed;
    out[39] = p.vortex;
    out[40] = p.attractor;
    out[41] = p.trailLength;
    out[42] = p.trailTaper;
    out[43] = static_cast<f32>(p.auxCount);
    out[44] = p.auxAt;
    out[45] = p.auxLife;
    out[46] = p.auxSpeed;
    out[47] = p.auxSize;
    out[48] = p.auxSpread;
    out[49] = static_cast<f32>(p.collision);
    out[50] = p.collisionY;
    out[51] = p.collisionBounce;
    out[52] = static_cast<f32>(p.blendMode);
    out[53] = static_cast<f32>(p.maxParticles);
    out[54] = static_cast<f32>(p.emitterSpace);
    out[55] = static_cast<f32>(p.emitFrom);
    out[56] = p.auxProbability;
    out[57] = p.trailWidth;
    out[58] = p.trailOpacity;
    out[59] = p.sizeRandom;
    out[60] = p.opacityRandom;
    out[61] = p.colorRandom;
    out[62] = p.collisionCenter.x;
    out[63] = p.collisionCenter.z;
    out[64] = p.collisionRadius;
    out[65] = p.collisionBox.x;
    out[66] = p.collisionBox.y;
    out[67] = p.collisionBox.z;
    out[68] = p.meshScale;
    out[69] = p.meshLit ? 1.0f : 0.0f;
    static_assert(static_cast<u32>(ParticleParam::Count) == 70, "query_particles: um valor por ParticleParam");
    return true;
}

// --- Aurea Particular 8.2: o que vem de OUTRA camada / asset -----------------
// Cada troca é UM passo de desfazer (history_ + modelRevision_), como os
// parâmetros. As ligações são por id: apagar a fonte não quebra nada — o
// renderizador não acha a camada e volta para a caixa do emissor.
namespace {
Layer* particle_layer(Composition* comp, u64 layerId) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    return l && l->kind == LayerKind::ParticleSystem ? l : nullptr;
}
} // namespace

bool Engine::set_particle_source(u64 layerId, u64 sourceLayerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = particle_layer(comp, layerId);
    if (!l) return false;
    if (sourceLayerId != 0) {
        // A fonte vive na MESMA composição e não é a própria camada.
        const Layer* src = comp->layer(LayerId::unpack(sourceLayerId));
        if (!src || sourceLayerId == layerId) return false;
    }
    if (l->particles.emitterSource == sourceLayerId) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "fonte das particulas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->particles.emitterSource = sourceLayerId;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_particle_texture(u64 layerId, u64 assetOrImageLayer) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = particle_layer(comp, layerId);
    if (!l) return false;
    u64 asset = 0;
    if (assetOrImageLayer != 0) {
        // Primeiro como CAMADA de imagem (o que a UI lista); senão, como asset
        // de imagem já carregado. O que não for imagem é recusado.
        const Layer* il = comp->layer(LayerId::unpack(assetOrImageLayer));
        if (il && il->kind == LayerKind::Image) asset = il->source.pack();
        else if (images_.count(assetOrImageLayer)) asset = assetOrImageLayer;
        else return false;
    }
    if (l->particles.textureAsset == asset) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "imagem das particulas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->particles.textureAsset = asset;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_particle_mesh(u64 layerId, u64 modelLayerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = particle_layer(comp, layerId);
    if (!l) return false;
    if (modelLayerId != 0) {
        const Layer* ml = comp->layer(LayerId::unpack(modelLayerId));
        if (!ml || ml->kind != LayerKind::Model3D) return false;
    }
    if (l->particles.meshSource == modelLayerId) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "malha das particulas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->particles.meshSource = modelLayerId;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_particle_life_curves(u64 layerId, u32 kind, const f32* values, u32 count) noexcept {
    if (kind > 2 || (count > 0 && !values)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = particle_layer(comp, layerId);
    if (!l) return false;
    count = std::min(count, ParticleData::kMaxLifeStops);
    history_.before_mutation(*comp, project_->timeline().current(),
                             kind == 0 ? "cor ao longo da vida" : kind == 1 ? "tamanho ao longo da vida" : "opacidade ao longo da vida");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    ParticleData& p = l->particles;
    // Posição presa em 0..1 e valores sem NaN: o shader assume isso.
    auto fin = [](f32 v, f32 lo, f32 hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; };
    if (kind == 0) {
        p.colorStopCount = count;
        for (u32 i = 0; i < count; ++i) {
            const f32* v = values + i * 4;
            p.colorStops[i] = Vec4{fin(v[0], 0, 1), fin(v[1], 0, 1), fin(v[2], 0, 1), fin(v[3], 0, 1)};
        }
    } else {
        Vec2* dst = kind == 1 ? p.sizeCurve : p.opacityCurve;
        (kind == 1 ? p.sizeCurveCount : p.opacityCurveCount) = count;
        for (u32 i = 0; i < count; ++i) {
            const f32* v = values + i * 2;
            dst[i] = Vec2{fin(v[0], 0, 1), fin(v[1], 0, kind == 1 ? 20.0f : 1.0f)};
        }
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_particle_links(u64 layerId, u64* out4) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = particle_layer(comp, layerId);
    if (!l || !out4) return false;
    const ParticleData& p = l->particles;
    // A UI lista CAMADAS: a textura sai também como a 1ª camada de imagem
    // que usa aquele asset (0 = nenhuma na composição).
    u64 texLayer = 0;
    if (p.textureAsset != 0) {
        const OrderedIds<LayerId>& order = comp->order();
        for (u32 i = 0; i < order.size() && !texLayer; ++i) {
            const Layer* il = comp->layer(order.at(i));
            if (il && il->kind == LayerKind::Image && il->source.pack() == p.textureAsset) texLayer = order.at(i).pack();
        }
    }
    out4[0] = p.emitterSource;
    out4[1] = texLayer;
    out4[2] = p.meshSource;
    out4[3] = p.textureAsset;
    return true;
}

u32 Engine::query_particle_curve(u64 layerId, u32 kind, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = particle_layer(comp, layerId);
    if (!l || !out || kind > 2) return 0;
    const ParticleData& p = l->particles;
    const u32 per = kind == 0 ? 4u : 2u;
    const u32 n = std::min(kind == 0 ? p.colorStopCount : kind == 1 ? p.sizeCurveCount : p.opacityCurveCount,
                           std::min(ParticleData::kMaxLifeStops, capacity / per));
    for (u32 i = 0; i < n; ++i) {
        if (kind == 0) {
            const Vec4 c = p.colorStops[i];
            out[i * 4] = c.x; out[i * 4 + 1] = c.y; out[i * 4 + 2] = c.z; out[i * 4 + 3] = c.w;
        } else {
            const Vec2 c = kind == 1 ? p.sizeCurve[i] : p.opacityCurve[i];
            out[i * 2] = c.x; out[i * 2 + 1] = c.y;
        }
    }
    return n;
}

bool Engine::set_time_remap(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->timeRemapEnabled == on) return l != nullptr;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "remapear o tempo" : "desligar remapeamento");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    if (on && l->timeRemap.keys.empty()) {
        // Curva equivalente ao tempo de agora: tocar igual até alguém mexer.
        const f64 s0 = l->source_frame(l->start), s1 = l->source_frame(l->end);
        l->timeRemap.property = TrackProperty::TimeRemap;
        l->timeRemap.set(l->local_time(l->start), static_cast<f32>(s0));
        l->timeRemap.set(l->local_time(l->end), static_cast<f32>(s1));
    }
    l->timeRemapEnabled = on;
    project_->mark_dirty();
    request_render();
    return true;
}

namespace {
/// Último quadro da fonte em quadros da composição (0 = sem limite conhecido).
f64 source_last_frame(const Project& project, const Layer& l, f64 compFps) noexcept {
    const Asset* a = project.asset(l.source);
    if (!a || a->duration.value <= 0 || a->timebaseFps <= 0.0) return 0.0;
    return std::max(0.0, static_cast<f64>(a->duration.value) * compFps / a->timebaseFps - 1.0);
}
} // namespace

u32 Engine::query_time_remap(u64 layerId, f32* out, u32 maxFloats) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !out || maxFloats < 5) return 0;
    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    const Track& t = l->timeRemap;
    const FrameIndex now = playback_.current();
    const f64 speed = l->source_frame(FrameIndex{now.value + 1}) - l->source_frame(now);
    out[0] = static_cast<f32>(t.keys.size());
    out[1] = static_cast<f32>(l->local_time(l->start).value);
    out[2] = static_cast<f32>(l->local_time(l->end).value);
    out[3] = static_cast<f32>(source_last_frame(*project_, *l, fps));
    out[4] = static_cast<f32>(speed);
    u32 w = 5;
    for (const Keyframe& k : t.keys) {
        if (w + 7 > maxFloats) break;
        out[w++] = static_cast<f32>(k.time.value);
        out[w++] = k.value;
        out[w++] = static_cast<f32>(static_cast<u8>(k.interp));
        out[w++] = k.bx1;
        out[w++] = k.by1;
        out[w++] = k.bx2;
        out[w++] = k.by2;
    }
    return w;
}

bool Engine::set_time_remap_value(u64 layerId, i64 localFrame, f32 sourceFrame) noexcept {
    if (!std::isfinite(sourceFrame)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    // Apply a queued seek and begin-undo before editing. The gesture supplies
    // its fixed timeline key, so decoder feedback cannot move the edit target.
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || localFrame < l->local_time(l->start).value || localFrame > l->local_time(l->end).value) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "tempo do vídeo");
    enable_time_remap_curve(*l);
    const FrameIndex key{localFrame};
    const u32 at = l->timeRemap.find_exact(key);
    const f64 last = source_last_frame(*project_, *l, comp->fps() > 0 ? comp->fps() : 30);
    const f32 value = last > 0 ? std::clamp(sourceFrame, 0.f, static_cast<f32>(last)) : std::max(0.f, sourceFrame);
    write_time_remap(*l, key, value, at == kInvalidIndex ? Interpolation::Linear : l->timeRemap.keys[at].interp);
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::edit_time_remap_key(u64 layerId, i32 index, i64 localFrame, f32 sourceFrame, i32 interp) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !l->timeRemapEnabled || interp > static_cast<i32>(Interpolation::Steps)) return -1;
    Track& t = l->timeRemap;
    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    const f64 last = source_last_frame(*project_, *l, fps);
    auto clampValue = [&](f32 v) { return last > 0.0 ? std::clamp(v, 0.0f, static_cast<f32>(last)) : std::max(0.0f, v); };
    const i64 lo = l->local_time(l->start).value, hi = l->local_time(l->end).value;
    history_.before_mutation(*comp, project_->timeline().current(), index < 0 ? "ponto na curva de tempo" : "mover ponto da curva de tempo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    i32 at = index;
    if (index < 0) {
        const FrameIndex f{std::clamp(localFrame, lo, hi)};
        if (t.find_exact(f) != kInvalidIndex) return static_cast<i32>(t.find_exact(f));
        const f32 v = t.sample_keys(f);
        // A curva que o ponto corta: o novo ponto herda a interpolação do trecho.
        const u32 before = t.find_before(f);
        const Interpolation in = before != kInvalidIndex ? t.keys[before].interp : Interpolation::Linear;
        at = static_cast<i32>(t.set(f, v, interp >= 0 ? static_cast<Interpolation>(interp) : in));
    } else {
        if (static_cast<u32>(index) >= t.keys.size()) return -1;
        Keyframe& k = t.keys[static_cast<u32>(index)];
        // Tempo preso entre os vizinhos (a ordem não muda); pontas presas no lugar.
        const i64 minT = index > 0 ? t.keys[static_cast<u32>(index) - 1].time.value + 1 : k.time.value;
        const i64 maxT = static_cast<u32>(index) + 1 < t.keys.size() ? t.keys[static_cast<u32>(index) + 1].time.value - 1 : k.time.value;
        k.time = FrameIndex{std::clamp(localFrame, minT, std::max(minT, maxT))};
        k.value = clampValue(sourceFrame);
        if (interp >= 0) k.interp = static_cast<Interpolation>(interp);
    }
    t.lastIndex = 0;
    project_->mark_dirty();
    request_render();
    return at;
}

bool Engine::remove_time_remap_key(u64 layerId, u32 index) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || index >= l->timeRemap.keys.size() || l->timeRemap.keys.size() <= 2) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "apagar ponto da curva de tempo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->timeRemap.keys.erase(l->timeRemap.keys.begin() + index);
    l->timeRemap.lastIndex = 0;
    project_->mark_dirty();
    request_render();
    return true;
}

namespace {
/// O easing do trecho visto de trás para a frente: g(u) = 1 − f(1 − u). Exato
/// para linear, entrada/saída e bézier (inclusive a força, que é composição e
/// comuta com o espelho); Segurar/Quique/Elástico/Degraus ficam como estão.
void mirror_remap_ease(Keyframe& k) noexcept {
    switch (k.interp) {
        case Interpolation::Bounce: if (k.by2 == -10.f) k.bx2 = k.bx2 < .5f ? 1.f : 0.f; break;
        case Interpolation::EaseIn: k.interp = Interpolation::EaseOut; break;
        case Interpolation::EaseOut: k.interp = Interpolation::EaseIn; break;
        case Interpolation::Bezier:
        case Interpolation::CustomCurve: {
            const f32 x1 = k.bx1, y1 = k.by1, x2 = k.bx2, y2 = k.by2;
            k.bx1 = 1.0f - x2; k.by1 = 1.0f - y2;
            k.bx2 = 1.0f - x1; k.by2 = 1.0f - y1;
            break;
        }
        default: break;
    }
}
} // namespace

bool Engine::reverse_time_remap(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->end.value - l->start.value < 2 || l->timeRemap.has_expression()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "remapear ao contrário");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    enable_time_remap_curve(*l);
    Track& t = l->timeRemap;
    // O espelho vale para os quadros que APARECEM: [lo, hi − 1]. Se as chaves
    // não cobrem o clipe inteiro, a curva está parada além delas: uma chave
    // com esse mesmo valor na ponta é exata (não corta trecho nenhum).
    const i64 lo = l->local_time(l->start).value, last = l->local_time(l->end).value - 1;
    if (t.keys.empty() || t.keys.front().time.value > lo)
        (void)t.set(FrameIndex{lo}, t.sample_keys(FrameIndex{lo}), Interpolation::Linear);
    if (t.keys.back().time.value < last)
        (void)t.set(FrameIndex{last}, t.sample_keys(FrameIndex{last}), Interpolation::Linear);
    const std::vector<Keyframe> old = t.keys;
    const usize n = old.size();
    std::vector<Keyframe> out(n);
    for (usize j = 0; j < n; ++j) {
        const Keyframe& src = old[n - 1 - j];
        Keyframe k = src;
        k.time = FrameIndex{lo + last - src.time.value};
        k.tangentIn = -src.tangentOut;
        k.tangentOut = -src.tangentIn;
        // O trecho que SAI desta chave era o que CHEGAVA nela: o easing mora
        // na chave da esquerda, então vem da vizinha anterior (espelhado).
        if (j + 1 < n) {
            const Keyframe& seg = old[n - 2 - j];
            k.interp = seg.interp;
            k.bx1 = seg.bx1; k.by1 = seg.by1; k.bx2 = seg.bx2; k.by2 = seg.by2;
            k.easePower = seg.easePower;
            k.easingPreset = seg.easingPreset;
            mirror_remap_ease(k);
        }
        out[j] = k;
    }
    t.keys = std::move(out);
    t.lastIndex = 0;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_keep_pitch(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    if (l->keepPitch == on) return true;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "manter o tom do audio" : "tom segue a velocidade");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->keepPitch = on;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::apply_speed_ramp(u64 layerId, u32 preset) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || preset > 7) return false;
    const i64 dur = l->end.value - l->start.value;
    if (dur < 2) return false;
    if (preset == 7) {
        // Congelar aqui: o quadro do cabeçote fica parado 1 s e depois o
        // vídeo segue na velocidade normal da camada. As chaves ANTES do
        // cabeçote ficam (dá para congelar em vários pontos, da esquerda para
        // a direita); as depois dele dão lugar ao congelamento.
        const i64 lo = l->local_time(l->start).value, hi = l->local_time(l->end).value;
        const i64 t0 = std::clamp(l->local_time(playback_.current()).value, lo, hi);
        if (t0 >= hi) return false;   // no último quadro não há o que segurar
        history_.before_mutation(*comp, project_->timeline().current(), "congelar aqui");
        modelRevision_.fetch_add(1, std::memory_order_acq_rel);
        const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        // Velocidade normal = a da camada sem a curva (velocidade/reverso).
        const bool had = l->timeRemapEnabled;
        l->timeRemapEnabled = false;
        const f64 rate = (l->source_frame(l->end) - l->source_frame(l->start)) / static_cast<f64>(dur);
        l->timeRemapEnabled = had;
        enable_time_remap_curve(*l);
        Track& t = l->timeRemap;
        const f32 v0 = t.sample_keys(FrameIndex{t0});
        std::erase_if(t.keys, [&](const Keyframe& k) { return k.time.value > t0; });
        t.lastIndex = 0;
        write_time_remap(*l, FrameIndex{t0}, v0, Interpolation::Linear);
        const i64 t1 = std::min(hi, t0 + std::max<i64>(1, std::llround(fps)));
        write_time_remap(*l, FrameIndex{t1}, v0, Interpolation::Linear);
        if (t1 < hi) {
            const f64 last = source_last_frame(*project_, *l, fps);
            f64 v = static_cast<f64>(v0) + rate * static_cast<f64>(hi - t1);
            v = last > 0.0 ? std::clamp(v, 0.0, last) : std::max(0.0, v);
            write_time_remap(*l, FrameIndex{hi}, static_cast<f32>(v), Interpolation::Linear);
        }
        t.lastIndex = 0;
        project_->mark_dirty();
        request_render();
        return true;
    }
    history_.before_mutation(*comp, project_->timeline().current(), "rampa de velocidade");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    // O trecho da fonte que a camada mostra agora (sem a curva antiga).
    const f32 freezeFrame = static_cast<f32>(l->source_frame(playback_.current()));
    const bool had = l->timeRemapEnabled;
    l->timeRemapEnabled = false;
    const f64 s0 = l->source_frame(l->start), s1 = l->source_frame(l->end);
    (void)had;
    const f64 span = s1 - s0;
    Track& t = l->timeRemap;
    t.clear();
    t.property = TrackProperty::TimeRemap;
    const i64 k0 = l->local_time(l->start).value;
    auto key = [&](f64 u, f64 v, Interpolation in, f32 bx1 = 0.33f, f32 by1 = 0.0f, f32 bx2 = 0.67f, f32 by2 = 1.0f) {
        const FrameIndex at{k0 + static_cast<i64>(std::llround(u * static_cast<f64>(dur)))};
        const u32 i = t.set(at, static_cast<f32>(s0 + v * span), in);
        if (i < t.keys.size()) {
            t.keys[i].bx1 = bx1; t.keys[i].by1 = by1; t.keys[i].bx2 = bx2; t.keys[i].by2 = by2;
        }
    };
    switch (preset) {
        case 0: key(0, 0, Interpolation::Linear); key(1, 1, Interpolation::Linear); break;
        // Suave / acelerar / desacelerar usam as facilidades puras — as mesmas
        // que o efeito Remapear tempo mostra por chave, sem gráfico.
        case 1: key(0, 0, Interpolation::EaseInOut); key(1, 1, Interpolation::Linear); break;
        case 2:
            // Rápido (1,5× a média) → lento (0,25×) → rápido; cantos suavizados.
            key(0.0, 0.00, Interpolation::Bezier, 0.33f, 0.33f, 0.80f, 0.95f);
            key(0.3, 0.45, Interpolation::Linear);
            key(0.7, 0.55, Interpolation::Bezier, 0.20f, 0.05f, 0.67f, 0.67f);
            key(1.0, 1.00, Interpolation::Linear);
            break;
        case 3: key(0, 0, Interpolation::EaseIn); key(1, 1, Interpolation::Linear); break;
        case 4: key(0, 0, Interpolation::EaseOut); key(1, 1, Interpolation::Linear); break;
        case 5:
            t.set(FrameIndex{k0}, freezeFrame, Interpolation::Hold);
            t.set(FrameIndex{k0 + dur}, freezeFrame, Interpolation::Hold);
            break;
        case 6: key(0, 1, Interpolation::Linear); key(1, 0, Interpolation::Linear); break;
        default: break;
    }
    l->timeRemapEnabled = true;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_motion_blur(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "ligar desfoque de movimento" : "desligar desfoque de movimento");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->motionBlur = on;
    if (on) comp->motion_blur().enabled = true;
    project_->mark_dirty();
    request_render();
    return true;
}

namespace {
/// Minúsculas ASCII sem acento: o UTF-8 é decodificado e as letras latinas
/// acentuadas (Latin-1 e Latin Extended-A — português, espanhol, francês,
/// europeu central) viram a letra base. O resto passa como está, o que basta
/// para "contém".
std::string fold_for_search(const std::string& s) {
    // U+00C0..U+00FF → letra base ('0' = mantém: ×, ÷).
    static constexpr char kLatin1[] = "aaaaaaaceeeeiiii" "dnooooo0ouuuuyts" "aaaaaaaceeeeiiii" "dnooooo0ouuuuyty";
    // U+0100..U+017F (pares maiúscula/minúscula da mesma base; Ĳ ĳ mantidos).
    static constexpr char kExtA[] = "aaaaaa" "cccccccc" "dddd" "eeeeeeeeee" "gggggggg" "hhhh" "iiiiiiiiii" "00" "jj" "kkk"
                                    "llllllllll" "nnnnnnnnn" "oooooo" "oo" "rrrrrr" "ssssssss" "tttttt" "uuuuuuuuuuuu"
                                    "ww" "yyy" "zzzzzz" "s";
    static_assert(sizeof(kLatin1) == 64 + 1 && sizeof(kExtA) == 128 + 1, "tabelas de acento");
    std::string out;
    out.reserve(s.size());
    for (usize i = 0; i < s.size();) {
        const u8 c = static_cast<u8>(s[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
            ++i;
            continue;
        }
        u32 cp = 0, len = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; len = 4; }
        bool ok = len != 0 && i + len <= s.size();
        for (u32 k = 1; ok && k < len; ++k) {
            const u8 cc = static_cast<u8>(s[i + k]);
            ok = (cc & 0xC0) == 0x80;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (!ok) { out.push_back(static_cast<char>(c)); ++i; continue; }   // UTF-8 quebrado: byte cru
        char base = '0';
        if (cp >= 0xC0 && cp <= 0xFF) base = kLatin1[cp - 0xC0];
        else if (cp >= 0x100 && cp <= 0x17F) base = kExtA[cp - 0x100];
        if (base != '0') out.push_back(base);
        else out.append(s, i, len);
        i += len;
    }
    return out;
}
} // namespace

bool Engine::set_layer_adjustment(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    if (l->adjustment == on) return true;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "camada de ajuste" : "desligar camada de ajuste");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->adjustment = on;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_layer_guide(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    if (l->guide == on) return true;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "camada guia" : "desligar camada guia");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->guide = on;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_layer_label(u64 layerId, u32 label) noexcept {
    if (label >= kLayerLabelCount) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    if (l->label == label) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "etiqueta da camada");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->label = static_cast<u8>(label);
    project_->mark_dirty();
    return true;   // só organização: nenhum pixel muda
}

bool Engine::set_layer_solo(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    if (l->solo == on) return true;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "solo" : "desligar solo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->solo = on;
    project_->mark_dirty();
    request_render();
    return true;
}

std::vector<u64> Engine::search_layers(const std::string& query) noexcept {
    std::vector<u64> out;
    const std::string q = fold_for_search(query);
    if (q.empty()) return out;
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return out;
    const u32 n = comp->order().size();
    for (u32 i = 0; i < n; ++i) {
        const LayerId id = comp->order().at(n - 1 - i);   // a frente primeiro
        const Layer* l = comp->layer(id);
        if (!l) continue;
        const bool hit = fold_for_search(l->name).find(q) != std::string::npos
                      || (l->kind == LayerKind::Text && fold_for_search(l->text.content).find(q) != std::string::npos);
        if (hit) out.push_back(id.pack());
    }
    return out;
}

bool Engine::set_frame_blend(u64 layerId, u32 mode) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Video || mode > 2) return false;
    history_.before_mutation(*comp, project_->timeline().current(), mode ? "mistura de quadros" : "sem mistura de quadros");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->frameBlend = static_cast<u8>(mode);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_vector_blur(u64 layerId, f32 amount) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Video) return false;
    history_.before_mutation(*comp, project_->timeline().current(), amount > 0.0f ? "desfoque do movimento do video" : "sem desfoque do video");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->vectorBlur = std::clamp(amount, 0.0f, 2.0f);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_composition_motion_blur(bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "motion blur da composicao");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    comp->motion_blur().enabled = on;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_motion_blur(bool& on, f32& shutter) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    on = comp->motion_blur().enabled;
    shutter = comp->motion_blur().shutterAngle;
    return true;
}

bool Engine::set_shutter_angle(f32 degrees) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "obturador");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    comp->motion_blur().shutterAngle = std::clamp(degrees, 0.0f, 720.0f);
    project_->mark_dirty();
    request_render();
    return true;
}

// =============================================================================
// Gizmo 3D
// =============================================================================
namespace {
bool lives_in_3d(const Layer& l) noexcept {
    return l.threeD || l.kind == LayerKind::Model3D || l.kind == LayerKind::Camera || l.kind == LayerKind::Light
        || l.transform.rotation.x != 0.0f || l.transform.rotation.y != 0.0f || l.transform.position.z != 0.0f;
}
} // namespace

std::string Engine::playback_report() noexcept {
    std::lock_guard<std::mutex> lock(perfMutex_);
    return playbackMeasurement_.report;
}

bool Engine::set_raw_playback(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    LayerId chosen{};
    if (enabled && comp) comp->layers().for_each([&](LayerId id, const Layer& layer) {
        if (!chosen.valid() && layer.kind == LayerKind::Video) chosen = id;
    });
    if (enabled && !chosen.valid()) return false;
    rawPlaybackLayer_ = chosen;
    { std::lock_guard<std::mutex> perfLock(perfMutex_); playbackMeasurement_ = {}; }
    audio_.stop();
    audioComp_ = nullptr;
    playback_.seek(FrameIndex{0}, monotonic_ns());
    request_render();
    return true;
}

void Engine::set_scene_editor(bool enabled, f32 yaw, f32 pitch, f32 distance) noexcept {
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(distance)) return;
    std::lock_guard<std::mutex> lock(modelMutex_);
    sceneEditor_ = SceneEditorView{enabled, std::remainder(yaw, 360.0f), std::clamp(pitch, -80.0f, 80.0f), std::clamp(distance, 0.25f, 30.0f)};
    request_render();
}

u32 Engine::query_scene_guides(f32* lines, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !sceneEditor_.enabled || !lines || capacity == 0) return 0;
    const Mat4 vp = scene_editor_projection(comp->width(), comp->height(), sceneEditor_);
    u32 count = 0;
    auto line = [&](Vec3 a, Vec3 b, f32 kind) {
        if (count >= capacity) return;
        const Vec4 x = vp * Vec4{a.x, a.y, a.z, 1}, y = vp * Vec4{b.x, b.y, b.z, 1};
        if (x.w <= 1e-4f || y.w <= 1e-4f) return;
        f32* p = lines + count++ * 5;
        p[0] = x.x / x.w; p[1] = x.y / x.w; p[2] = y.x / y.w; p[3] = y.y / y.w; p[4] = kind;
    };
    const f32 width = static_cast<f32>(comp->width()), height = static_cast<f32>(comp->height());
    // Quadro da composição (z = 0) em quartos: grade densa por cima do objeto
    // 3D parecia "o texto virou uma grade" (relato beta).
    for (u32 i = 0; i <= 4; ++i) {
        const f32 t = static_cast<f32>(i) / 4;
        line(Vec3{width * t, 0, 0}, Vec3{width * t, height, 0}, 0);
        line(Vec3{0, height * t, 0}, Vec3{width, height * t, 0}, 0);
    }
    const auto time = playback_.current();
    for (u32 i = 0; i < comp->order().size(); ++i) {
        const Layer* layer = comp->layer(comp->order().at(i));
        if (!layer || !layer->visible || !layer->contains_time(time)) continue;
        if (layer->kind != LayerKind::Camera && layer->kind != LayerKind::Null && layer->kind != LayerKind::Light) continue;
        const Mat4 world = layer_world_3d(*comp, *layer, time);
        const Vec3 anchor = layer->kind == LayerKind::Camera ? Vec3{} : layer->transform.anchor;
        const Vec3 origin = world.transform_point(anchor);
        if (layer->kind == LayerKind::Null || layer->kind == LayerKind::Light) {
            const f32 d = height * 0.035f;
            line(origin - Vec3{d, 0, 0}, origin + Vec3{d, 0, 0}, 2);
            line(origin - Vec3{0, d, 0}, origin + Vec3{0, d, 0}, 2);
            line(origin - Vec3{0, 0, d}, origin + Vec3{0, 0, d}, 2);
            continue;
        }
        const Vec3 forward = Vec3{world.col[2].x, world.col[2].y, world.col[2].z}.normalized();
        const Vec3 right = Vec3{world.col[1].x, world.col[1].y, world.col[1].z}.cross(forward).normalized();
        const Vec3 down = forward.cross(right).normalized();
        const f32 fov = camera_fov_deg_at(*layer, static_cast<f64>(layer->local_time(time).value));
        const f32 length = height * 0.4f, halfY = length * std::tan(std::clamp(fov, 1.0f, 170.0f) * kDeg2Rad * 0.5f);
        const f32 halfX = halfY * width / std::max(1.0f, height);
        const Vec3 center = origin + forward * length;
        const Vec3 corners[4] = {center - right * halfX - down * halfY, center + right * halfX - down * halfY,
            center + right * halfX + down * halfY, center - right * halfX + down * halfY};
        for (u32 j = 0; j < 4; ++j) { line(origin, corners[j], 1); line(corners[j], corners[(j + 1) % 4], 1); }
    }
    return count;
}

bool Engine::query_gizmo(u64 layerId, f32 length, f32* out, bool localSpace) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !out || !lives_in_3d(*l)) return false;
    const FrameIndex now = playback_.current();
    const Mat4 w = layer_world_3d(*comp, *l, now);
    const FrameIndex local = l->local_time(now);
    const Vec3 a = l->kind == LayerKind::Model3D ? Vec3{0, 0, 0} : Vec3{
        l->tracks.sample_or(TrackProperty::AnchorX, local, l->transform.anchor.x),
        l->tracks.sample_or(TrackProperty::AnchorY, local, l->transform.anchor.y),
        l->tracks.sample_or(TrackProperty::AnchorZ, local, l->transform.anchor.z)};
    const Vec4 o4 = w * Vec4{a.x, a.y, a.z, 1};
    const Vec3 o{o4.x, o4.y, o4.z};
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
        : comp_view_projection(*comp, now);
    auto proj = [&](Vec3 p, f32* xy) {
        const Vec4 c = vp * Vec4{p.x, p.y, p.z, 1};
        if (!(c.w > 1e-6f)) return false;
        xy[0] = c.x / c.w;
        xy[1] = c.y / c.w;
        return true;
    };
    // Mundo da cena: X para a direita, Y para BAIXO (px da composição), Z para
    // dentro da tela — os mesmos eixos da posição da camada.
    auto axis = [&](u32 index) {
        if (localSpace) return Vec3{w.col[index].x, w.col[index].y, w.col[index].z}.normalized() * length;
        return Vec3{index == 0 ? length : 0, index == 1 ? length : 0, index == 2 ? length : 0};
    };
    return proj(o, out) && proj(o + axis(0), out + 2) && proj(o + axis(1), out + 4)
        && proj(o + axis(2), out + 6);
}

namespace { Rect vector_content_box(const Layer& l, FrameIndex local); }

// Seleção na Cena 3D pelo MESMO mundo/câmera do render: o raio da câmera
// de navegação (ou da ativa) contra o corpo de cada camada 3D visível —
// plano da caixa (texto, forma, mídia) ou triângulos (modelo, texto 3D,
// forma 3D) — e ganha o acerto mais perto. Sem corpo sob o dedo, a origem
// mais perto na tela até `radius` (câmera, luz, nulo, objeto miúdo).
u64 Engine::scene_pick(f32 compX, f32 compY, f32 radius) noexcept {
    if (!std::isfinite(compX) || !std::isfinite(compY)) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return 0;
    const FrameIndex now = playback_.current();
    const u32 w = std::max(1u, comp->width()), h = std::max(1u, comp->height());
    const scene3d::SceneCamera cam = sceneEditor_.enabled ? scene_editor_camera(w, h, sceneEditor_) : comp_camera(*comp, now, w, h);
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(w, h, sceneEditor_) : comp_view_projection(*comp, now);
    Vec3 origin, dir;
    if (!scene_ray(cam, w, h, compX, compY, origin, dir)) return 0;
    u64 bodyId = 0, originId = 0;
    f32 bodyBest = std::numeric_limits<f32>::max();
    f32 originBest = std::isfinite(radius) && radius > 0.0f ? radius : 0.0f;
    const OrderedIds<LayerId>& order = comp->order();
    for (u32 i = 0; i < order.size(); ++i) {
        const LayerId id = order.at(i);
        const Layer* l = comp->layer(id);
        if (!l || !l->visible || l->locked || !l->contains_time(now) || l->kind == LayerKind::Audio) continue;
        if (!wants_layer_3d(*comp, *l, now) && !lives_in_3d(*l)) continue;
        const Mat4 world = layer_world_3d(*comp, *l, now);
        if (l->kind == LayerKind::Model3D) {
            if (auto asset = model_lookup(this, l->model.scene))
                if (ray_hits_model_layer(*comp, *l, now, *asset, origin, dir, bodyBest)) bodyId = id.pack();
        } else if (l->kind != LayerKind::Camera && l->kind != LayerKind::Light && l->kind != LayerKind::Null) {
            bridge::LayerDetailPOD pod{};
            if (fill_layer_detail_locked(id.pack(), pod) && pod.sourceWidth > 0 && pod.sourceHeight > 0) {
                Vec2 o{0.0f, 0.0f};
                if (l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector) {
                    const Rect b = vector_content_box(*l, l->local_time(now));
                    o = Vec2{b.x, b.y};
                }
                // Raio × plano z = 0 da camada, no espaço do mundo.
                const Vec3 p0 = world.transform_point(Vec3{o.x, o.y, 0});
                const Vec3 ex = world.transform_point(Vec3{o.x + 1, o.y, 0}) - p0;
                const Vec3 ey = world.transform_point(Vec3{o.x, o.y + 1, 0}) - p0;
                const Vec3 n = ex.cross(ey);
                const f32 denom = n.dot(dir);
                if (std::fabs(denom) > 1e-12f) {
                    const f32 t = n.dot(p0 - origin) / denom;
                    if (t > 0.0f && t < bodyBest) {
                        // Ponto → coordenadas da camada (base ex/ey, não ortogonal).
                        const Vec3 r = origin + dir * t - p0;
                        const f32 a = ex.dot(ex), b = ex.dot(ey), c = ey.dot(ey), d = r.dot(ex), e = r.dot(ey);
                        const f32 det = a * c - b * b;
                        if (std::fabs(det) > 1e-20f) {
                            const f32 u = (d * c - b * e) / det, v = (a * e - b * d) / det;
                            if (u >= 0.0f && v >= 0.0f && u <= static_cast<f32>(pod.sourceWidth) && v <= static_cast<f32>(pod.sourceHeight)) {
                                bodyBest = t; bodyId = id.pack();
                            }
                        }
                    }
                }
            }
        }
        // Origem (o pivô que o gizmo mostra): o alvo de quem não tem corpo.
        const Vec3 a = l->kind == LayerKind::Model3D || l->kind == LayerKind::Camera ? Vec3{} : l->transform.anchor;
        const Vec4 c = vp * world * Vec4{a.x, a.y, a.z, 1};
        if (c.w > 1e-6f) {
            const f32 dist = std::hypot(c.x / c.w - compX, c.y / c.w - compY);
            if (dist < originBest) { originBest = dist; originId = id.pack(); }
        }
    }
    return bodyId ? bodyId : originId;
}

bool Engine::query_preview_gesture_basis(u64 layerId, f32* out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !out || !lives_in_3d(*l) || l->locked) return false;
    const auto now = playback_.current(), local = l->local_time(now);
    if (!l->contains_time(now)) return false;
    for (u32 i = 0; i < 3; ++i) {
        out[i] = l->tracks.sample_or(static_cast<TrackProperty>(i), local, (&l->transform.position.x)[i]);
        out[9+i] = l->tracks.sample_or(static_cast<TrackProperty>(6+i), local, (&l->transform.rotation.x)[i]);
    }
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
        : comp_view_projection(*comp, now);
    const Layer* parent = l->parent.valid() ? comp->layer(l->parent) : nullptr;
    const Mat4 pw = (parent ? layer_world_3d(*comp, *parent, now) : Mat4::identity()) * l->parentBasis;
    Vec4 c = vp * pw * Vec4{out[0], out[1], out[2], 1};
    // A selected active camera is at the eye. Pan it at the composition plane's depth.
    if (!(c.w > 1e-5f)) c = vp * Vec4{comp->width() * .5f, comp->height() * .5f, 0, 1};
    if (!(c.w > 1e-5f)) return false;
    const Mat4 inverse = inverse4(pw) * inverse4(vp);
    const Vec3 projected{c.x/c.w, c.y/c.w, c.z/c.w};
    const Vec3 origin = inverse.transform_point(projected);
    const Vec3 x = inverse.transform_point(projected + Vec3{1,0,0}) - origin;
    const Vec3 y = inverse.transform_point(projected + Vec3{0,1,0}) - origin;
    out[3]=x.x; out[4]=x.y; out[5]=x.z;
    out[6]=y.x; out[7]=y.y; out[8]=y.z;
    out[12] = 180.f / std::max(1.f, static_cast<f32>(comp->height()));
    for (u32 i=0; i<13; ++i) if (!std::isfinite(out[i])) return false;
    return true;
}

bool Engine::gizmo_move_local(u64 layerId, u32 axis, f32 amount, f32* out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !out || axis > 5 || !std::isfinite(amount)) return false;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    auto s = [&](TrackProperty prop, f32 fallback) {
        const Track* tr = l->tracks.find(prop);
        return tr ? tr->value_or(local, fallback) : fallback;
    };
    Vec3 pos{s(TrackProperty::PositionX, l->transform.position.x), s(TrackProperty::PositionY, l->transform.position.y),
             s(TrackProperty::PositionZ, l->transform.position.z)};
    Vec3 d{axis == 0 ? amount : 0.0f, axis == 1 ? amount : 0.0f, axis == 2 ? amount : 0.0f};
    // 0..2 = world axes; 3..5 = the evaluated object's local axes in world space.
    if (axis >= 3) {
        const Mat4 world = layer_world_3d(*comp, *l, now);
        const auto& column = world.col[axis - 3];
        d = Vec3{column.x, column.y, column.z}.normalized() * amount;
    }
    // Com pai, o passo no mundo vira passo no espaço do pai (só a parte linear).
    const Layer* p = l->parent.valid() ? comp->layer(l->parent) : nullptr;
    if (p || l->hasParentBasis) {
        const Mat4 pw = (p ? layer_world_3d(*comp, *p, now) : Mat4::identity()) * l->parentBasis;
        const Mat4 inv = inverse4(pw);
        const Vec4 v = inv * Vec4{d.x, d.y, d.z, 0};
        d = Vec3{v.x, v.y, v.z};
    }
    out[0] = pos.x + d.x;
    out[1] = pos.y + d.y;
    out[2] = pos.z + d.z;
    return true;
}

// =============================================================================
// Ambiente 3D (HDRI)
// =============================================================================
namespace {
std::shared_ptr<scene3d::HdriPixels> read_hdri_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (512l << 20)) { std::fclose(f); return nullptr; }
    std::vector<u8> bytes(static_cast<usize>(n));
    const usize got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got != bytes.size()) return nullptr;
    return scene3d::decode_hdri(bytes.data(), bytes.size());
}
} // namespace

std::shared_ptr<const scene3d::HdriPixels> Engine::hdri_lookup(void* selfPtr, AssetId id) {
    // Dentro do prepare (modelo travado): o cache; se faltar (projeto reaberto),
    // lê do arquivo uma vez.
    auto* self = static_cast<Engine*>(selfPtr);
    if (auto it = self->hdris_.find(id.pack()); it != self->hdris_.end()) return it->second;
    const Asset* a = self->project_ ? self->project_->asset(id) : nullptr;
    if (!a || a->kind != AssetKind::Environment) return nullptr;
    std::shared_ptr<const scene3d::HdriPixels> px = read_hdri_file(self->resolve_asset_path(a->sourcePath));
    self->hdris_[id.pack()] = px;   // nulo também fica: não tenta de novo a cada quadro
    return px;
}

Result<u64> Engine::import_hdri(const char* path, u64 objectLayer) noexcept {
    if (!path || !*path) return Status{Errc::InvalidArgument, "sem arquivo"};
    const std::string resolved = resolve_asset_path(path);
    std::shared_ptr<scene3d::HdriPixels> px = read_hdri_file(resolved);
    if (!px) return Status{Errc::UnsupportedFormat, "Ambiente nao lido: use HDR/HDRI Radiance ou JPG/PNG panoramico, ate 8 megapixels"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Layer* object = objectLayer ? comp->layer(LayerId::unpack(objectLayer)) : nullptr;
    if (objectLayer && (!object || !(object->threeD || object->kind == LayerKind::Model3D)))
        return Status{Errc::InvalidArgument, "objeto 3D nao encontrado"};
    history_.before_mutation(*comp, project_->timeline().current(), object ? "hdri do objeto" : "hdri");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    Asset asset;
    asset.kind = AssetKind::Environment;
    std::string name = path;
    if (const usize s = name.find_last_of("/\\"); s != std::string::npos) name = name.substr(s + 1);
    asset.name = name;
    asset.sourcePath = store_asset_path(resolved);
    const AssetId id = project_->add_asset(std::move(asset));
    hdris_[id.pack()] = px;
    if (object) {
        object->environmentSource = 1; object->environmentAsset = id.pack();
        object->environmentIntensity = 1; object->environmentRotation = 0; object->environmentExposure = 1;
    } else { comp->environment().hdri = id; }
    project_->mark_dirty();
    request_render();
    AUREA_LOG_INFO("hdri: %ux%u", px->width, px->height);
    return id.pack();
}

bool Engine::clear_hdri() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !comp->environment().hdri.valid()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "estudio neutro");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    comp->environment().hdri = AssetId{};
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_environment_background(bool visible) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    if (comp->environment().showBackground == visible) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "Environment Texture");
    comp->environment().showBackground = visible;
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty(); request_render(); return true;
}
bool Engine::environment_background() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    return comp && comp->environment().showBackground;
}

bool Engine::set_environment_params(f32 intensity, f32 rotationDeg) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "ambiente");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    comp->environment().intensity = std::clamp(intensity, 0.0f, 20.0f);
    comp->environment().rotation = std::fmod(rotationDeg, 360.0f);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_environment(f32* out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !out) return false;
    out[0] = comp->environment().hdri.valid() ? 1.0f : 0.0f;
    out[1] = comp->environment().intensity;
    out[2] = comp->environment().rotation;
    return true;
}

// --- Estúdio procedural e chão do 3D (v32) ------------------------------------------
Result<u64> Engine::set_studio_environment(u32 preset, u64 objectLayer) noexcept {
    if (preset >= scene3d::kStudioPresetCount) return Status{Errc::InvalidArgument, "preset de estudio invalido"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Layer* object = objectLayer ? comp->layer(LayerId::unpack(objectLayer)) : nullptr;
    if (objectLayer && (!object || !(object->threeD || object->kind == LayerKind::Model3D)))
        return Status{Errc::InvalidArgument, "objeto 3D nao encontrado"};
    history_.before_mutation(*comp, project_->timeline().current(), object ? "estudio do objeto" : "estudio");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const u64 key = preset ? scene3d::studio_environment_key(preset) : 0ull;
    if (object) {
        object->environmentSource = preset ? 1u : 0u;
        object->environmentAsset = key;
        if (preset) { object->environmentIntensity = 1; object->environmentRotation = 0; object->environmentExposure = 1; }
    } else {
        comp->environment().studioPreset = preset;
        // O estúdio escolhido vence: um HDRI importado antes sai (o asset fica no projeto).
        if (preset) comp->environment().hdri = AssetId{};
    }
    project_->mark_dirty();
    request_render();
    return key;
}

u32 Engine::studio_environment() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    return comp ? comp->environment().studioPreset : 0u;
}

bool Engine::set_scene_floor(u32 mode, f32 r, f32 g, f32 b, f32 roughness, f32 reflectivity, f32 contactShadow,
                             f32 fade) noexcept {
    for (f32 v : {r, g, b, roughness, reflectivity, contactShadow, fade}) if (!std::isfinite(v)) return false;
    if (mode > 2u) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "chao 3D");
    FloorSettings& fl = comp->floor();
    fl.mode = mode;
    fl.color = Color{std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f), 1.0f};
    fl.roughness = std::clamp(roughness, 0.0f, 1.0f);
    fl.reflectivity = std::clamp(reflectivity, 0.0f, 1.0f);
    fl.contactShadow = std::clamp(contactShadow, 0.0f, 1.0f);
    fl.fade = std::clamp(fade, 1.0f, 100.0f);
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_scene_floor(f32* out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !out) return false;
    const FloorSettings& fl = comp->floor();
    out[0] = static_cast<f32>(fl.mode);
    out[1] = fl.color.r; out[2] = fl.color.g; out[3] = fl.color.b;
    out[4] = fl.roughness; out[5] = fl.reflectivity; out[6] = fl.contactShadow; out[7] = fl.fade;
    return true;
}

// --- Qualidade e pós do 3D ---------------------------------------------------------
bool Engine::set_scene3d_quality(u32 tier) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || tier >= kScene3DQualityCount) return false;
    if (comp->post_process().quality3d == tier) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "qualidade 3D");
    comp->post_process().quality3d = tier;
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::scene3d_quality() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    return comp ? comp->post_process().quality3d : 0u;
}

bool Engine::set_scene3d_tonemap(u32 op, f32 exposure) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || op > 1u || !std::isfinite(exposure)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "tone map 3D");
    comp->post_process().toneMapper = op;
    comp->post_process().exposure = std::clamp(exposure, 0.01f, 64.0f);
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_scene3d_bloom(bool on, f32 intensity, f32 threshold) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !std::isfinite(intensity) || !std::isfinite(threshold)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "bloom 3D");
    PostProcessSettings& pp = comp->post_process();
    pp.bloom = on;
    pp.bloomIntensity = std::clamp(intensity, 0.0f, 4.0f);
    pp.bloomThreshold = std::clamp(threshold, 0.0f, 64.0f);
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_scene3d_post(f32* out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !out) return false;
    const PostProcessSettings& pp = comp->post_process();
    out[0] = static_cast<f32>(pp.quality3d);
    out[1] = static_cast<f32>(pp.toneMapper);
    out[2] = pp.exposure;
    out[3] = pp.bloom ? 1.0f : 0.0f;
    out[4] = pp.bloomIntensity;
    out[5] = pp.bloomThreshold;
    return true;
}

bool Engine::set_object_environment(u64 layerId, u32 source, u64 hdriAsset, f32 intensity, f32 rotationDeg,
                                    f32 exposure) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    // Só objeto que vive no 3D tem ambiente próprio.
    if (!l || !(l->threeD || l->kind == LayerKind::Model3D)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "ambiente do objeto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->environmentSource = std::min<u32>(source, 1u);
    l->environmentAsset = hdriAsset;
    l->environmentIntensity = std::clamp(intensity, 0.0f, 20.0f);
    l->environmentRotation = std::fmod(rotationDeg, 360.0f);
    l->environmentExposure = std::clamp(exposure, 0.05f, 20.0f);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_object_environment(u64 layerId, f32* out, u64* outAsset) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !out) return false;
    out[0] = static_cast<f32>(l->environmentSource);
    out[1] = static_cast<f32>(l->environmentAsset);
    if (outAsset) *outAsset = l->environmentAsset;
    out[2] = l->environmentIntensity;
    out[3] = l->environmentRotation;
    out[4] = l->environmentExposure;
    return true;
}

bool Engine::set_model_shadows(u64 layerId, bool cast, bool receive) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "sombras do objeto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->model.castShadows = cast;
    l->model.receiveShadows = receive;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_model_shadows(u64 layerId, f32* out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !out || l->kind != LayerKind::Model3D) return false;
    out[0] = l->model.castShadows ? 1.0f : 0.0f;
    out[1] = l->model.receiveShadows ? 1.0f : 0.0f;
    return true;
}

// =============================================================================
// Pré-composição
// =============================================================================
Result<u64> Engine::precompose(const u64* ids, u32 count, const char* name) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !ids || count == 0) return Status{Errc::InvalidArgument, "nada para pre-compor"};
    Timeline& tl = project_->timeline();
    const CompositionId parentId = tl.current();
    // Camadas na ordem vertical (de baixo para cima) e o trecho que ocupam.
    std::vector<LayerId> moving;
    i64 lo = std::numeric_limits<i64>::max(), hi = 0;
    i32 top = -1;
    for (u32 i = 0; i < comp->order().size(); ++i) {
        const LayerId id = comp->order().at(i);
        for (u32 k = 0; k < count; ++k) {
            if (ids[k] != id.pack()) continue;
            const Layer* l = comp->layer(id);
            if (!l) break;
            moving.push_back(id);
            lo = std::min(lo, l->start.value);
            hi = std::max(hi, l->end.value);
            top = static_cast<i32>(i);
        }
    }
    if (moving.empty()) return Status{Errc::NotFound, "camadas nao encontradas"};
    history_.before_mutation(*comp, parentId, "pre-compor");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    u32 n = 1;
    tl.for_each_composition([&](CompositionId, const Composition&) { ++n; });
    const std::string label = name && *name ? std::string(name) : "Pré-composição " + std::to_string(n - 1);
    const CompositionId cid = tl.create_composition(label, comp->width(), comp->height(), comp->fps());
    Composition* child = tl.composition(cid);
    comp = tl.composition(parentId);   // a criação pode ter realocado
    if (!child || !comp) return Status{Errc::OutOfMemory, "composicao nao criada"};
    child->set_duration(comp->duration());
    child->set_transparent_background(true);
    child->set_nesting_depth(comp->nesting_depth() + 1);
    // Copia (mantendo tempos) e remapeia pais DENTRO do grupo; pai de fora
    // fica para trás (a camada perderia a referência — solta).
    std::vector<std::pair<LayerId, LayerId>> map;
    for (LayerId id : moving) {
        const Layer* src = comp->layer(id);
        const LayerId nid = child->add_layer(src->kind, src->name);
        if (Layer* dst = child->layer(nid)) {
            *dst = *src;
            map.emplace_back(id, nid);
        }
    }
    const FrameIndex now = playback_.current();
    for (auto& [oldId, nid] : map) {
        Layer* dst = child->layer(nid);
        if (!dst || !dst->parent.valid()) continue;
        LayerId np{};
        for (auto& [o, nn] : map) if (o == dst->parent) np = nn;
        if (!np.valid() && (dst->hasParentBasis || (dst->kind != LayerKind::Model3D && transform_is_static(*dst)))) {
            // Pai ficou de fora: a camada leva o lugar de MUNDO que ocupava
            // (senão o transform local passaria a valer sozinho e ela pularia).
            if (const Layer* orig = comp->layer(oldId)) {
                if (dst->hasParentBasis) {
                    if (const Layer* oldParent = comp->layer(orig->parent))
                        dst->parentBasis = layer_world_3d(*comp, *oldParent, now) * dst->parentBasis;
                } else set_local_from(*dst, layer_world_matrix(*comp, *orig, now));
            }
        }
        dst->parent = np;
    }
    child->rebuild_draw_order();
    for (LayerId id : moving) {
        media_.close_layer(id);
        comp->remove_layer(id);
    }
    // A camada que mostra a pré-composição: ocupa o trecho, tempo 1:1.
    const LayerId pl = comp->add_layer(LayerKind::Composition, label);
    Layer* p = comp->layer(pl);
    if (!p) return Status{Errc::OutOfMemory, "camada nao criada"};
    p->nested.composition = cid;
    p->start = FrameIndex{lo};
    p->end = FrameIndex{hi};
    p->offset = FrameIndex{lo};
    const Vec3 center{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    p->transform.anchor = center;
    p->transform.position = center;
    p->transform.scale = Vec3{1, 1, 1};
    p->transform.rotation = Vec3{0, 0, 0};
    p->transform.opacity = 1.0f;
    const i32 target = std::max(0, top - static_cast<i32>(moving.size()) + 1);
    (void)comp->reorder_layer(pl, static_cast<u32>(target));
    comp->rebuild_draw_order();
    selection_.assign(1, pl.pack());
    project_->mark_dirty();
    request_render();
    return pl.pack();
}

Result<u32> Engine::ungroup_precomp(u64 layerId, std::string* why) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const LayerId pid = LayerId::unpack(layerId);
    const Layer* P = comp ? comp->layer(pid) : nullptr;
    if (!P || P->kind != LayerKind::Composition) return Status{Errc::InvalidArgument, "nao e uma pre-composicao"};
    Timeline& tl = project_->timeline();
    const CompositionId parentId = tl.current();
    const Composition* C = tl.composition(P->nested.composition);
    if (!C) return Status{Errc::NotFound, "composicao ausente"};
    auto refuse = [&](const char* reason) {
        if (why) *why = reason;
        return Status{Errc::NotSupported, reason};
    };
    const FrameIndex pLocal = P->local_time(P->start);
    if (!P->effects.empty()) return refuse("a camada do grupo tem efeitos");
    if (!P->masks.empty()) return refuse("a camada do grupo tem mascaras");
    if (P->blendMode != BlendMode::Normal) return refuse("a camada do grupo tem modo de mistura");
    if (const Track* o = P->tracks.find(TrackProperty::Opacity); (o && o->animated())
        || std::fabs(P->tracks.sample_or(TrackProperty::Opacity, pLocal, P->transform.opacity) - 1.0f) > 1e-4f)
        return refuse("a camada do grupo tem opacidade");
    if (P->threeD) return refuse("a camada do grupo esta em 3D");
    if (P->timeRemapEnabled || P->speed != 1.0f || P->reversed || P->speed_track()) return refuse("o tempo do grupo foi alterado");
    if (P->transitionIn || P->transitionOut || P->echoCount || P->rgbDelay > 0.0f) return refuse("o grupo tem transicao ou eco");
    if (!C->transparent_background()) return refuse("o grupo tem fundo proprio");
    bool camOrLight = false;
    for (u32 i = 0; i < C->order().size(); ++i) {
        if (const Layer* x = C->layer(C->order().at(i))) camOrLight |= x->kind == LayerKind::Camera || x->kind == LayerKind::Light;
    }
    if (camOrLight) return refuse("o grupo tem camera ou luz propria");

    history_.before_mutation(*comp, parentId, "desagrupar");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    // Quadro c da filha aparece no quadro c + shift da mãe; só o trecho que a
    // camada mostrava (e que existe na filha) continua visível.
    const i64 shift = P->start.value - P->offset.value;
    const i64 visLo = std::max(P->start.value, shift), visHi = std::min(P->end.value, shift + C->duration().value);
    u32 index = 0;
    for (u32 i = 0; i < comp->order().size(); ++i) if (comp->order().at(i) == pid) index = i;
    // Transform do grupo: identidade = direto; senão um Nulo com o transform
    // (e keyframes) da camada vira o pai das raízes.
    bool identity = !P->tracks.has_animation() && !P->parent.valid();
    if (identity) {
        const Mat4 m = layer_world_matrix(*comp, *P, P->start);
        const Mat4 I = Mat4::identity();
        for (int c = 0; c < 4 && identity; ++c)
            identity = std::fabs(m.col[c].x - I.col[c].x) < 1e-4f && std::fabs(m.col[c].y - I.col[c].y) < 1e-4f
                    && std::fabs(m.col[c].z - I.col[c].z) < 1e-4f && std::fabs(m.col[c].w - I.col[c].w) < 1e-4f;
    }
    const Layer group = *P;
    LayerId nullId{};
    if (!identity) {
        nullId = comp->add_layer(LayerKind::Null, "Grupo · " + group.name);
        if (Layer* n = comp->layer(nullId)) {
            n->start = group.start;
            n->end = group.end;
            n->offset = group.offset;
            n->parent = group.parent;
            n->transform = group.transform;
            n->transform.opacity = 1.0f;
            n->tracks = group.tracks;
            n->tracks.remove_if([](const Track& t) { return t.property == TrackProperty::Opacity; });
        }
    }
    // Cópias na ordem vertical da filha (de baixo para cima), pais remapeados.
    std::vector<std::pair<LayerId, LayerId>> map;
    std::vector<LayerId> added;
    std::vector<std::pair<u32, u32>> tracks;   // (linha no grupo, linha nova aqui)
    const auto ungroupedTrack = [&](u32 from) -> u32 {
        if (from == 0) return 0;
        for (const auto& [was, now] : tracks) if (was == from) return now;
        tracks.emplace_back(from, comp->new_track_id());
        return tracks.back().second;
    };
    for (u32 i = 0; i < C->order().size(); ++i) {
        const LayerId cid = C->order().at(i);
        const Layer* src = C->layer(cid);
        if (!src) continue;
        i64 s = src->start.value + shift, e = src->end.value + shift;
        const i64 cutIn = std::max<i64>(0, visLo - s);
        s = std::max(s, visLo);
        e = std::min(e, visHi);
        if (e <= s) continue;   // fora do trecho mostrado: não aparecia
        const LayerId nid = comp->add_layer(src->kind, src->name);
        Layer* dst = comp->layer(nid);
        if (!dst) continue;
        *dst = *src;
        // As linhas de dentro do grupo são números de OUTRA composição: aqui
        // viram linhas novas (as que dividiam linha lá continuam juntas).
        dst->trackId = ungroupedTrack(src->trackId);
        dst->start = FrameIndex{s};
        dst->end = FrameIndex{e};
        // Aparar a entrada: o conteúdo continua no mesmo lugar do tempo.
        if (cutIn > 0) {
            dst->offset = FrameIndex{src->offset.value + (dst->timeRemapEnabled || dst->speed == 1.0f
                                                              ? cutIn
                                                              : static_cast<i64>(std::llround(static_cast<f64>(cutIn) * dst->speed)))};
        }
        map.emplace_back(cid, nid);
        added.push_back(nid);
    }
    for (auto& [oldId, nid] : map) {
        Layer* dst = comp->layer(nid);
        if (!dst) continue;
        LayerId np{};
        for (auto& [o, nn] : map) if (o == dst->parent) np = nn;
        dst->parent = np.valid() ? np : nullId;
    }
    // Quem era filho da camada do grupo passa a ser do Nulo (ou do pai dela).
    for (u32 i = 0; i < comp->order().size(); ++i) {
        Layer* x = comp->layer(comp->order().at(i));
        if (x && x->parent == pid) x->parent = nullId.valid() ? nullId : group.parent;
    }
    media_.close_layer(pid);
    comp->remove_layer(pid);
    // No lugar do grupo: as camadas na mesma ordem, o Nulo logo acima delas.
    u32 at = index;
    for (LayerId id : added) (void)comp->reorder_layer(id, at++);
    if (nullId.valid()) (void)comp->reorder_layer(nullId, at);
    comp->rebuild_draw_order();
    selection_.clear();
    for (LayerId id : added) selection_.push_back(id.pack());
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(added.size());
}

bool Engine::open_precomp(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Composition) return false;
    Timeline& tl = project_->timeline();
    if (!tl.composition(l->nested.composition)) return false;
    // Cabeçote no tempo correspondente dentro da filha.
    const f64 f = l->source_frame(playback_.current());
    compStack_.push_back(tl.current());
    tl.set_current(l->nested.composition);
    selection_.clear();
    if (Composition* child = current_composition()) {
        playback_.configure(child->fps(), child->duration());
        playback_.seek(FrameIndex{std::clamp<i64>(static_cast<i64>(f), 0, child->duration().value - 1)}, monotonic_ns());
    }
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    request_render();
    return true;
}

bool Engine::close_precomp() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_ || compStack_.empty()) return false;
    Timeline& tl = project_->timeline();
    const CompositionId back = compStack_.back();
    compStack_.pop_back();
    if (!tl.composition(back)) return false;
    tl.set_current(back);
    selection_.clear();
    if (Composition* c = current_composition()) playback_.configure(c->fps(), c->duration());
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    request_render();
    return true;
}

std::string Engine::current_composition_name() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* c = project_ ? current_composition() : nullptr;
    return c ? c->name() : std::string{};
}

u32 Engine::precomp_depth() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return static_cast<u32>(compStack_.size());
}

// =============================================================================
// Copiar e colar
// =============================================================================
u32 Engine::copy_layers(const u64* ids, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !ids) return 0;
    clipboard_.layers.clear();
    clipboard_.layersAnchor = std::numeric_limits<i64>::max();
    for (u32 i = 0; i < count; ++i) {
        if (const Layer* l = comp->layer(LayerId::unpack(ids[i]))) {
            clipboard_.layers.emplace_back(ids[i], *l);
            clipboard_.layersAnchor = std::min(clipboard_.layersAnchor, l->start.value);
        }
    }
    // Ordem vertical preservada ao colar: de baixo para cima.
    std::sort(clipboard_.layers.begin(), clipboard_.layers.end(), [comp](const auto& a, const auto& b) {
        return comp->z_index_of(LayerId::unpack(a.first)) < comp->z_index_of(LayerId::unpack(b.first));
    });
    return static_cast<u32>(clipboard_.layers.size());
}

u32 Engine::paste_layers(i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || clipboard_.layers.empty()) return 0;
    history_.before_mutation(*comp, project_->timeline().current(), "colar camadas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const i64 delta = std::max<i64>(0, frame) - clipboard_.layersAnchor;
    std::vector<std::pair<u64, LayerId>> made;
    std::vector<std::pair<u32, u32>> tracks;   // (linha no recorte, linha nova aqui)
    const auto pastedTrack = [&](u32 from) -> u32 {
        if (from == 0) return 0;
        for (const auto& [was, now] : tracks) if (was == from) return now;
        tracks.emplace_back(from, comp->new_track_id());
        return tracks.back().second;
    };
    for (const auto& [oldPack, src] : clipboard_.layers) {
        // Mídia de outro projeto não existe aqui: essa camada não entra.
        if (src.source.valid() && !project_->asset(src.source)) continue;
        if (src.kind == LayerKind::Composition && !project_->timeline().composition(src.nested.composition)) continue;
        const LayerId lid = comp->add_layer(src.kind, src.name);
        Layer* dst = comp->layer(lid);
        if (!dst) continue;
        *dst = src;
        // A cópia nunca cai na LINHA do original (senão as duas andariam juntas
        // na timeline); copiadas da mesma linha continuam juntas numa linha nova.
        dst->trackId = pastedTrack(src.trackId);
        dst->start = FrameIndex{std::max<i64>(0, src.start.value + delta)};
        dst->end = FrameIndex{std::max<i64>(dst->start.value + 1, src.end.value + delta)};
        made.emplace_back(oldPack, lid);
    }
    // Pai: se veio junto, o novo; se ainda existe aqui, o mesmo; senão, solto.
    for (const auto& [oldPack, lid] : made) {
        Layer* dst = comp->layer(lid);
        if (!dst || !dst->parent.valid()) continue;
        LayerId np{};
        for (const auto& [op, nl] : made) if (op == dst->parent.pack()) np = nl;
        if (!np.valid() && comp->layer(dst->parent)) np = dst->parent;
        dst->parent = np;
    }
    comp->rebuild_draw_order();
    selection_.clear();
    for (const auto& [op, lid] : made) selection_.push_back(lid.pack());
    std::sort(selection_.begin(), selection_.end());
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(made.size());
}

bool Engine::copy_style(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    clipboard_.style = *l;
    clipboard_.hasStyle = true;
    return true;
}

namespace {
/// Keys ficam em tempo LOCAL, e o início da camada é o `offset` (ponto de
/// entrada na fonte). Levar uma trilha para outra camada mantém a distância do
/// início dela: soma a diferença dos pontos de entrada. Sem isso, efeitos de um
/// vídeo aparado em 10 s colavam com os keys 10 s adiante numa imagem.
void rebase_track(Track& t, i64 delta) noexcept {
    if (delta == 0) return;
    for (Keyframe& k : t.keys) k.time.value += delta;
    t.lastIndex = 0;
}
/// Troca a pilha de efeitos de `dst` pela de `src` (com os keyframes deles).
void replace_effects(Layer& dst, const std::vector<EffectInstance>& effects, const TrackSet& srcTracks, i64 delta) {
    dst.tracks.remove_if([](const Track& t) { return t.property == TrackProperty::EffectParam; });
    dst.effects = effects;
    for (const EffectInstance& e : effects) {
        if (e.id != kInvalidIndex) dst.nextEffectId = std::max(dst.nextEffectId, e.id + 1);
    }
    for (u32 i = 0; i < srcTracks.size(); ++i) {
        if (srcTracks.at(i).property != TrackProperty::EffectParam) continue;
        Track t = srcTracks.at(i);
        rebase_track(t, delta);
        dst.tracks.add(std::move(t));
    }
}
/// Copia uma track de transform/aparência (valor e keyframes) de `src`.
void copy_track(Layer& dst, const Layer& src, TrackProperty p) {
    dst.tracks.remove_if([p](const Track& t) { return t.property == p && t.effectIndex == kInvalidIndex; });
    if (const Track* t = src.tracks.find(p)) {
        Track c = *t;
        rebase_track(c, dst.offset.value - src.offset.value);
        dst.tracks.add(std::move(c));
    }
}
} // namespace

u32 Engine::paste_style(const u64* ids, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !ids || !clipboard_.hasStyle) return 0;
    history_.before_mutation(*comp, project_->timeline().current(), "colar estilo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const Layer& s = clipboard_.style;
    u32 done = 0;
    for (u32 i = 0; i < count; ++i) {
        Layer* d = comp->layer(LayerId::unpack(ids[i]));
        if (!d) continue;
        d->blendMode = s.blendMode;
        d->transform.opacity = s.transform.opacity;
        copy_track(*d, s, TrackProperty::Opacity);
        replace_effects(*d, s.effects, s.tracks, d->offset.value - s.offset.value);
        if (d->kind == LayerKind::Text && s.kind == LayerKind::Text) {
            const std::string content = d->text.content;
            d->text = s.text;
            d->text.content = content;
        }
        if (d->kind == LayerKind::Shape && s.kind == LayerKind::Shape) {
            d->shape.fillColor = s.shape.fillColor;
            d->shape.filled = s.shape.filled;
            d->shape.strokeColor = s.shape.strokeColor;
            d->shape.strokeWidth = s.shape.strokeWidth;
        }
        ++done;
    }
    project_->mark_dirty();
    request_render();
    return done;
}

u32 Engine::copy_effects(u64 layerId, u32 effectId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    if (effectId != kInvalidIndex && std::none_of(l->effects.begin(),l->effects.end(),[&](const auto& e){return e.id == effectId;})) return 0;
    clipboard_.effects.clear();
    for (const auto& e : l->effects) if (effectId == kInvalidIndex || e.id == effectId) clipboard_.effects.push_back(e);
    clipboard_.effectTracks.clear();
    clipboard_.effectsBase = l->offset.value;
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        const auto& track = l->tracks.at(i);
        if (track.property == TrackProperty::EffectParam && (effectId == kInvalidIndex || track.effectIndex == effectId)) clipboard_.effectTracks.push_back(track);
    }
    return static_cast<u32>(clipboard_.effects.size());
}

u32 Engine::paste_effects(const u64* ids, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !ids || clipboard_.effects.empty()) return 0;
    u32 done = 0;
    for (u32 i = 0; i < count; ++i) {
        Layer* d = comp->layer(LayerId::unpack(ids[i]));
        if (!d || d->locked) continue;
        if (done == 0) {
            history_.before_mutation(*comp, project_->timeline().current(), "colar efeitos");
            modelRevision_.fetch_add(1, std::memory_order_acq_rel);
        }
        // Ids novos depois do maior da camada: os keyframes seguem pelo id.
        u32 next = 0;
        for (const EffectInstance& e : d->effects) if (e.id != kInvalidIndex) next = std::max(next, e.id + 1);
        for (const EffectInstance& e : clipboard_.effects) {
            if ((e.type == effect_type_id(text::kAnimatorEffect) || e.type == effect_type_id(text::kTransformEffect)) && !supports_text_animation(*d)) continue;
            if ((e.type == effect_type_id(effect_keys::kText3DLayout) ||
                 ((e.type == effect_type_id(text::kAnimatorEffect) || e.type == effect_type_id(text::kTransformEffect)) && d->kind == LayerKind::Model3D)) &&
                !ensure_text3d_layout(*d).ok()) continue;
            // O layout das partes só serve em forma 3D (as partes já são nós próprios).
            if (e.type == effect_type_id(effect_keys::kShape3DLayout) && !is_shape3d_layer(*d)) continue;
            EffectInstance c = e;
            const u32 oldId = e.id;
            c.id = next++;
            for (const Track& t : clipboard_.effectTracks) {
                if (t.effectIndex != oldId) continue;
                Track nt = t;
                nt.effectIndex = c.id;
                rebase_track(nt, d->offset.value - clipboard_.effectsBase);
                d->tracks.add(std::move(nt));
            }
            d->effects.push_back(std::move(c));
        }
        // O contador acompanha: o próximo EffectAdd não pode repetir um id colado
        // (os keyframes de dois efeitos se misturariam).
        d->nextEffectId = std::max(d->nextEffectId, next);
        ++done;
    }
    if (done) { project_->mark_dirty(); request_render(); }
    return done;
}

u32 Engine::copy_keyframes(u64 layerId, i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    const FrameIndex local = l->local_time(FrameIndex{frame});
    clipboard_.keys.clear();
    clipboard_.pathKeys.clear();
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        const Track& t = l->tracks.at(i);
        const u32 k = t.find_exact(local);
        if (k == kInvalidIndex) continue;
        EffectTypeId type = 0;
        if (t.property == TrackProperty::EffectParam) {
            for (const EffectInstance& e : l->effects) if (e.id == t.effectIndex) type = e.type;
        }
        Keyframe key = t.keys[k];
        key.time = FrameIndex{0};
        u32 ordinal = 0;
        if (t.property == TrackProperty::EffectParam) {
            for (const EffectInstance& e : l->effects) {
                if (e.id == t.effectIndex) break;
                if (e.type == type) ++ordinal;
            }
        }
        clipboard_.keys.push_back(Clipboard::Key{t.property, t.effectIndex, t.effectParamIndex, type, key, ordinal});
    }
    return static_cast<u32>(clipboard_.keys.size());
}

namespace {
struct SelectedTrackKey { u32 track; u32 key; };
// Time remap is stored outside TrackSet. Every selection operation must resolve
// it through the same identity as single-key commands (including its effect alias).
const Track& selected_track(const Layer& layer, u32 index) {
    return index == kInvalidIndex ? layer.timeRemap : layer.tracks.at(index);
}
Track& selected_track(Layer& layer, u32 index) {
    return index == kInvalidIndex ? layer.timeRemap : layer.tracks.at(index);
}
bool resolve_selected_keys(const Layer& layer, const i64* refs, u32 count,
                           std::vector<SelectedTrackKey>& out) {
    if (!refs || count == 0 || count > 16384) return false;
    for (u32 i = 0; i < count; ++i) {
        const i64* ref = refs + i * 4;
        if (ref[0] < 0 || ref[0] > 65535 || ref[1] < -1 || ref[1] > UINT32_MAX
            || ref[2] < 0 || ref[2] > UINT32_MAX || ref[3] < INT32_MIN || ref[3] > INT32_MAX) return false;
        const TrackRef identity{{}, static_cast<TrackProperty>(ref[0]), static_cast<u32>(ref[1]), static_cast<u32>(ref[2])};
        const Track* track = read_command_track(layer, identity);
        if (!track) return false;
        const u32 key = track->find_exact(FrameIndex{ref[3]});
        if (key == kInvalidIndex) return false;
        u32 index = kInvalidIndex;
        if (track != &layer.timeRemap) {
            for (u32 t = 0; t < layer.tracks.size(); ++t) if (&layer.tracks.at(t) == track) { index = t; break; }
            if (index == kInvalidIndex) return false;
        }
        if (std::none_of(out.begin(), out.end(), [=](const auto& item) { return item.track == index && item.key == key; }))
            out.push_back({index, key});
    }
    return !out.empty();
}
}

u32 Engine::copy_keyframe_selection(u64 layerId, const i64* refs, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    // As referências vêm do modelo que a UI já viu: o que ela enfileirou antes
    // (inserir/mover keyframe) precisa estar aplicado.
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    std::vector<SelectedTrackKey> selected;
    if (!layer || !resolve_selected_keys(*layer, refs, count, selected)) return 0;
    i64 anchor = INT64_MAX;
    for (const auto& item : selected) anchor = std::min(anchor, selected_track(*layer, item.track).keys[item.key].time.value);
    std::vector<Clipboard::Key> copied;
    for (const auto& item : selected) {
        const Track& track = selected_track(*layer, item.track);
        Keyframe key = track.keys[item.key]; key.time.value -= anchor;
        EffectTypeId type = 0; u32 ordinal = 0;
        if (track.property == TrackProperty::EffectParam) {
            for (const auto& effect : layer->effects) if (effect.id == track.effectIndex) { type = effect.type; break; }
            for (const auto& effect : layer->effects) {
                if (effect.id == track.effectIndex) break;
                if (effect.type == type) ++ordinal;
            }
        }
        copied.push_back({track.property, track.effectIndex, track.effectParamIndex, type, key, ordinal});
    }
    clipboard_.keys = std::move(copied);
    clipboard_.pathKeys.clear();
    return static_cast<u32>(clipboard_.keys.size());
}

u32 Engine::edit_keyframe_selection(u64 layerId, const i64* refs, u32 count, i64 delta, bool remove) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    // Chamada direta, fora da fila: o `beginUndoGroup` que o gesto acabou de
    // enfileirar precisa valer ANTES desta mutação, senão o 1º passo do
    // arrasto vira um desfazer à parte (arrasto = UM passo).
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    std::vector<SelectedTrackKey> selected;
    if (!layer || layer->locked || (!remove && delta == 0) || delta < INT32_MIN || delta > INT32_MAX
        || !resolve_selected_keys(*layer, refs, count, selected)) return 0;
    std::vector<std::pair<u32, Track>> changed;
    for (u32 slot = 0; slot <= layer->tracks.size(); ++slot) {
        const u32 index = slot == layer->tracks.size() ? kInvalidIndex : slot;
        std::vector<u32> keys;
        for (const auto& item : selected) if (item.track == index) keys.push_back(item.key);
        if (keys.empty()) continue;
        const Track& original = selected_track(*layer, index);
        Track next = original;
        next.keys.clear();
        for (u32 k = 0; k < original.keys.size(); ++k) {
            const bool picked = std::find(keys.begin(), keys.end(), k) != keys.end();
            if (picked && remove) continue;
            Keyframe key = original.keys[k];
            if (picked) {
                const i64 target = key.time.value + delta;
                if (target < INT32_MIN || target > INT32_MAX) return 0;
                key.time.value = target;
            }
            next.keys.push_back(key);
        }
        std::sort(next.keys.begin(), next.keys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        for (usize k = 1; k < next.keys.size(); ++k) if (next.keys[k - 1].time == next.keys[k].time) return 0;
        next.lastIndex = 0;
        changed.emplace_back(index, std::move(next));
    }
    history_.before_mutation(*comp, project_->timeline().current(), remove ? "remover keyframes" : "mover keyframes");
    for (auto& entry : changed) selected_track(*layer, entry.first) = std::move(entry.second);
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty(); request_render();
    return static_cast<u32>(selected.size());
}

u32 Engine::paste_keyframes(const u64* ids, u32 count, i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !ids || (clipboard_.keys.empty() && clipboard_.pathKeys.empty()) || frame < INT32_MIN || frame > INT32_MAX)
        return 0;
    u32 placed = 0;
    for (u32 i = 0; i < count; ++i) {
        Layer* d = comp->layer(LayerId::unpack(ids[i]));
        if (!d || d->locked) continue;
        const FrameIndex local = d->local_time(FrameIndex{frame});
        if (std::any_of(clipboard_.keys.begin(), clipboard_.keys.end(), [&](const auto& key) {
            const i64 target = local.value + key.key.time.value;
            return target < INT32_MIN || target > INT32_MAX;
        })) continue;
        const bool pathsOut = std::any_of(clipboard_.pathKeys.begin(), clipboard_.pathKeys.end(), [&](const auto& pk) {
            return std::any_of(pk.keys.begin(), pk.keys.end(), [&](const PathKey& k) {
                const i64 target = local.value + k.frame;
                return target < INT32_MIN || target > INT32_MAX;
            });
        });
        if (pathsOut) continue;
        for (const Clipboard::Key& ck : clipboard_.keys) {
            u32 effectIndex = ck.effectIndex;
            if (ck.property == TrackProperty::MaskParam && (ck.effectParamIndex > 2 || !mask_by_id(*d, effectIndex))) continue;
            if (ck.property == TrackProperty::EffectParam) {
                // Match the same occurrence, so two Glows do not overwrite
                // each other's animation. Missing occurrences are skipped.
                effectIndex = kInvalidIndex;
                u32 ordinal = 0;
                for (const EffectInstance& e : d->effects) {
                    if (e.type == ck.effectType && ordinal++ == ck.effectOrdinal) {
                        effectIndex = e.id; break;
                    }
                }
                if (effectIndex == kInvalidIndex) continue;
            }
            if (placed == 0) {
                history_.before_mutation(*comp, project_->timeline().current(), "colar keyframes");
                modelRevision_.fetch_add(1, std::memory_order_acq_rel);
            }
            const FrameIndex target{local.value + ck.key.time.value};
            if (target.value < INT32_MIN || target.value > INT32_MAX) continue;
            Track& t = ck.property == TrackProperty::TimeRemap ? d->timeRemap
                : d->tracks.get_or_create(ck.property, effectIndex, ck.effectParamIndex);
            if (ck.property == TrackProperty::TimeRemap) d->timeRemapEnabled = true;
            const u32 k = t.set(target, ck.key.value, ck.key.interp);
            if (k < t.keys.size()) {
                // Curva inteira (bezier, tangentes, easing) do keyframe copiado.
                Keyframe nk = ck.key;
                nk.time = target;
                t.keys[k] = nk;
            }
            ++placed;
        }
        // Keyframes de forma (morph) da camada vetorial: no mesmo caminho
        // (grupo, caminho) do destino, se ele existir. Um key no mesmo quadro
        // é trocado; o caminho vira livre (o morph só existe no livre).
        if (d->kind == LayerKind::Shape && d->shape.shapeType == kShapeVector) {
            for (const Clipboard::PathKeys& pk : clipboard_.pathKeys) {
                if (pk.group >= d->shape.vector.groups.size() || pk.path >= d->shape.vector.groups[pk.group].paths.size())
                    continue;
                if (placed == 0) {
                    history_.before_mutation(*comp, project_->timeline().current(), "colar keyframes");
                    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
                }
                VectorPath& vp = d->shape.vector.groups[pk.group].paths[pk.path];
                if (vp.kind != VectorPathKind::Free) vector::make_editable(vp);
                for (PathKey k : pk.keys) {
                    k.frame += local.value;
                    auto at = std::lower_bound(vp.keys.begin(), vp.keys.end(), k.frame,
                                               [](const PathKey& a, i64 f) { return a.frame < f; });
                    if (at != vp.keys.end() && at->frame == k.frame) *at = std::move(k);
                    else vp.keys.insert(at, std::move(k));
                    ++placed;
                }
            }
        }
    }
    if (placed != 0) {
        project_->mark_dirty();
        request_render();
    }
    return placed;
}

u32 Engine::copy_animation(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    i64 anchor = INT64_MAX;
    for (u32 i = 0; i < l->tracks.size(); ++i)
        for (const Keyframe& k : l->tracks.at(i).keys) anchor = std::min(anchor, k.time.value);
    // Os keyframes de forma (morph) da camada vetorial também são animação.
    const bool vectorLayer = l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector;
    if (vectorLayer)
        for (const VectorGroup& g : l->shape.vector.groups)
            for (const VectorPath& p : g.paths)
                for (const PathKey& k : p.keys) anchor = std::min(anchor, k.frame);
    if (anchor == INT64_MAX) return 0;
    std::vector<Clipboard::PathKeys> copiedPaths;
    u32 pathCount = 0;
    if (vectorLayer) {
        const auto& groups = l->shape.vector.groups;
        for (u32 gi = 0; gi < groups.size(); ++gi) {
            for (u32 pi = 0; pi < groups[gi].paths.size(); ++pi) {
                const auto& keys = groups[gi].paths[pi].keys;
                if (keys.empty()) continue;
                Clipboard::PathKeys pk{gi, pi, keys};
                for (PathKey& k : pk.keys) k.frame -= anchor;
                pathCount += static_cast<u32>(pk.keys.size());
                copiedPaths.push_back(std::move(pk));
            }
        }
    }
    std::vector<Clipboard::Key> copied;
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        const Track& t = l->tracks.at(i);
        EffectTypeId type = 0;
        u32 ordinal = 0;
        if (t.property == TrackProperty::EffectParam) {
            for (const EffectInstance& e : l->effects) if (e.id == t.effectIndex) { type = e.type; break; }
            for (const EffectInstance& e : l->effects) {
                if (e.id == t.effectIndex) break;
                if (e.type == type) ++ordinal;
            }
        }
        for (Keyframe key : t.keys) {
            key.time.value -= anchor;
            copied.push_back(Clipboard::Key{t.property, t.effectIndex, t.effectParamIndex, type, key, ordinal});
        }
    }
    clipboard_.keys = std::move(copied);
    clipboard_.pathKeys = std::move(copiedPaths);
    return static_cast<u32>(clipboard_.keys.size()) + pathCount;
}

u32 Engine::optimize_keyframes(u64 layerId, i32 property, f32 tolerance) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || !(tolerance >= 0.0f)) return 0;
    // Tudo calculado em cópias: o desfazer só abre se algo sair.
    std::vector<std::pair<u32, Track>> changed;
    u32 removed = 0;
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        const Track& t = l->tracks.at(i);
        if (t.keys.size() < 3) continue;
        if (property >= 0 && static_cast<i32>(t.property) != property) continue;
        Track next = t;
        const f32 tol = std::max(1e-5f, tolerance * animation::value_range(t));
        const u32 n = animation::optimize_track(next, tol);
        if (n == 0) continue;
        removed += n;
        changed.emplace_back(i, std::move(next));
    }
    if (removed == 0) return 0;
    history_.before_mutation(*comp, project_->timeline().current(), "otimizar keyframes");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    for (auto& entry : changed) l->tracks.at(entry.first) = std::move(entry.second);
    project_->mark_dirty();
    request_render();
    return removed;
}

u32 Engine::clipboard_state() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return (clipboard_.layers.empty() ? 0u : 1u) | (clipboard_.hasStyle ? 2u : 0u)
         | (clipboard_.effects.empty() ? 0u : 4u) | (clipboard_.keys.empty() && clipboard_.pathKeys.empty() ? 0u : 8u);
}

// =============================================================================
// Presets (JSON; formato em project/Presets.hpp)
// =============================================================================
std::string Engine::save_preset(u64 layerId, presets::PresetKind kind, const std::string& name, u32 parts) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return {};
    presets::Preset p;
    if (!presets::capture(*l, kind, name, comp->fps(), parts, &effectRegistry_, p,
                         l->kind == LayerKind::Model3D && supports_text_animation(*l))) return {};
    return presets::write(p, &effectRegistry_);
}

std::string Engine::save_effect_preset(u64 layerId, u32 effectId, const std::string& name) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return {};
    presets::Preset p;
    if (!presets::capture_effect(*l, effectId, name, comp->fps(), &effectRegistry_, p)) return {};
    return presets::write(p, &effectRegistry_);
}

std::string Engine::import_alight_motion(const std::string& data, presets::AlightImportReport& report) noexcept {
    // Só lê o registro de efeitos (imutável depois da inicialização): não
    // precisa da trava do modelo, e um arquivo grande não segura a UI.
    return presets::import_alight_motion(data, &effectRegistry_, report);
}

bool Engine::apply_preset(u64 layerId, const std::string& json, i64 durationFrames, std::string* error) noexcept {
    // Lê e valida TUDO antes de travar/abrir o passo de desfazer: preset
    // quebrado não deixa passo vazio no histórico nem camada pela metade.
    presets::Preset p;
    std::string err;
    if (!presets::parse(json, p, &effectRegistry_, &err)) {
        if (error) *error = err;
        return false;
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) {
        if (error) *error = "camada nao encontrada";
        return false;
    }
    const bool meshText = l->kind == LayerKind::Model3D && supports_text_animation(*l);
    if (!presets::applicable(p, *l, meshText)) {
        if (error) *error = "este preset nao serve para esta camada";
        return false;
    }
    if (l->locked) {
        if (error) *error = "camada bloqueada";
        return false;
    }
    // O layout das partes (Shape 3D Layout) só existe em forma 3D.
    if (!is_shape3d_layer(*l) && std::any_of(p.effects.begin(), p.effects.end(), [](const EffectInstance& effect) {
        return effect.type == effect_type_id(effect_keys::kShape3DLayout);
    })) {
        if (error) *error = "este preset precisa de uma forma 3D";
        return false;
    }
    // A saved letter effect needs the same geometry preparation as EffectAdd.
    // Prepare a copy first: failure must not partially apply the preset or
    // replace the original recipe recorded by undo.
    if (!supports_text_animation(*l) && std::any_of(p.effects.begin(), p.effects.end(), [](const EffectInstance& effect) {
        return effect.type == effect_type_id(text::kTransformEffect) || effect.type == effect_type_id(text::kAnimatorEffect);
    })) {
        if (error) *error = "este preset precisa de texto";
        return false;
    }
    Layer prepared = *l;
    if ((meshText && p.kind == presets::PresetKind::Text) || std::any_of(p.effects.begin(), p.effects.end(), [&](const EffectInstance& effect) {
        return effect.type == effect_type_id(effect_keys::kText3DLayout) ||
               ((effect.type == effect_type_id(text::kAnimatorEffect) || effect.type == effect_type_id(text::kTransformEffect)) && l->kind == LayerKind::Model3D);
    })) {
        const Status ready = ensure_text3d_layout(prepared);
        if (!ready.ok()) {
            if (error) *error = "este preset precisa de texto 3D com ate 256 glifos";
            return false;
        }
    }
    history_.before_mutation(*comp, project_->timeline().current(), "aplicar preset");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    *l = std::move(prepared);
    // Animação: começa no cabeçote; fora da camada, no início dela.
    const FrameIndex ph = playback_.current();
    const i64 anchor = l->contains_time(ph) ? l->local_time(ph).value : l->local_time(l->start).value;
    const bool ok = presets::apply(p, *l, anchor, durationFrames, comp->fps(), &effectRegistry_, meshText);
    if (ok && p.kind == presets::PresetKind::Text && !meshText) recenter_text(*l);
    project_->mark_dirty();
    request_render();
    return ok;
}

void Engine::set_edit_mode(bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || comp->edit_mode() == on) return;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "modo edicao" : "modo composicao");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    comp->set_edit_mode(on);
    project_->mark_dirty();
}

bool Engine::edit_mode() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    return comp && comp->edit_mode();
}

bool Engine::set_layer_magnetic_track(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* current = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!current || current->locked || current->magneticTrack == on) return false;
    Command cmd;
    cmd.type = CommandType::LayerSetMagnetic;
    cmd.layer_magnetic.layer = LayerId::unpack(layerId);
    cmd.layer_magnetic.magnetic = on;
    const Status s = apply_command_internal(cmd, nullptr, true);
    return s.ok();
}

bool Engine::layer_magnetic_track(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    return l && l->magneticTrack;
}

bool Engine::ripple_delete(const u64* ids, u32 count) noexcept {
    if (!ids || count == 0) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    // Trecho que as camadas excluídas ocupavam: só buracos DENTRO dele fecham.
    i64 lo = std::numeric_limits<i64>::max(), hi = 0;
    for (u32 i = 0; i < count; ++i) {
        if (const Layer* l = comp->layer(LayerId::unpack(ids[i])); l && !l->locked) {
            lo = std::min(lo, l->start.value);
            hi = std::max(hi, l->end.value);
        }
    }
    if (hi <= 0) return false;
    // Cada trecho fecha o buraco na PRÓPRIA linha quando ela é magnética; fora
    // disso o buraco fica (é o que a pessoa pediu ao desligar). Uma linha
    // magnética nunca puxa os vizinhos de outra linha.
    std::vector<std::pair<u32, i64>> tracks;   // (linha, menor início dos trechos)
    for (u32 i = 0; i < count; ++i) {
        const Layer* l = comp->layer(LayerId::unpack(ids[i]));
        if (!l || l->locked || !l->magneticTrack) continue;
        const auto it = std::find_if(tracks.begin(), tracks.end(),
                                     [&](const std::pair<u32, i64>& t) { return t.first == l->trackId; });
        if (it == tracks.end()) tracks.emplace_back(l->trackId, l->start.value);
        else it->second = std::min(it->second, l->start.value);
    }
    // Fechar o buraco é o que ESTA ação faz desde sempre — a linha magnética
    // muda só o ALCANCE: com trechos magnéticos, cada um fecha a faixa dele;
    // sem nenhum, vale o comportamento de composição de sempre.
    const bool closeAll = tracks.empty();
    history_.before_mutation(*comp, project_->timeline().current(), "excluir e fechar o espaco");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    for (u32 i = 0; i < count; ++i) {
        const LayerId id = LayerId::unpack(ids[i]);
        const Layer* layer = comp->layer(id);
        if (!layer || layer->locked) continue;
        media_.close_layer(id);
        comp->remove_layer(id);
        selection_.erase(std::remove(selection_.begin(), selection_.end(), ids[i]), selection_.end());
    }
    if (closeAll) {
        comp->close_gaps(FrameIndex{lo}, FrameIndex{hi});
    } else {
        for (const auto& t : tracks) comp->close_gaps_in_track(FrameIndex{t.second}, FrameIndex{hi}, t.first);
    }
    comp->rebuild_draw_order();
    project_->mark_dirty();
    request_render();
    return true;
}

namespace {
bool has_clip_source(const Layer& l) noexcept {
    return l.kind == LayerKind::Video || l.kind == LayerKind::Audio || l.kind == LayerKind::Composition;
}

f64 clip_source_limit(const Project& project, const Composition& comp, const Layer& item) noexcept {
    if (item.kind == LayerKind::Composition) {
        if (const Composition* child = project.timeline().composition(item.nested.composition))
            return static_cast<f64>(child->duration().value - 1);
    } else if (const Asset* asset = project.asset(item.source); asset && asset->duration.value > 0 && asset->timebaseFps > 0) {
        return std::max(0.0, asset->duration.value * comp.fps() / asset->timebaseFps - 1.0);
    }
    return -1.0;
}

/// Velocidade com keyframes → curva de tempo equivalente (a integral amostrada
/// quadro a quadro, que é como a curva de tempo é lida). O `offset` também é o
/// relógio dos keyframes, então a entrada não pode mudar sem isto.
void materialize_speed_curve(Layer& l, i64 start, i64 end, f64 sourceLimit) noexcept {
    const i64 lo = std::min(start, l.start.value), hi = std::max(end, l.end.value);
    const i64 step = std::max<i64>(1, (hi - lo) / 4000);
    Track remap;
    remap.property = TrackProperty::TimeRemap;
    for (i64 f = lo; f < hi; f += step)
        remap.set(l.local_time(FrameIndex{f}), static_cast<f32>(l.source_frame(FrameIndex{f})));
    remap.set(l.local_time(FrameIndex{hi}), static_cast<f32>(l.source_frame(FrameIndex{hi})));
    // Alças da fonte: as pontas seguem na velocidade da ponta, para esticar o
    // trecho depois não congelar na borda antiga.
    if (sourceLimit >= 0) {
        constexpr f64 maxFrame = static_cast<f64>(i64{1} << 40);
        const f64 dir = l.reversed ? -1.0 : 1.0;
        const f64 v0 = l.speed_at(FrameIndex{lo}) * dir, v1 = l.speed_at(FrameIndex{hi}) * dir;
        const f64 s0 = l.source_frame(FrameIndex{lo}), s1 = l.source_frame(FrameIndex{hi});
        if (std::fabs(v0) > 1e-6) {
            const f64 t = std::clamp(static_cast<f64>(lo) + ((v0 > 0 ? 0.0 : sourceLimit) - s0) / v0, -maxFrame, maxFrame);
            if (std::isfinite(t) && t < static_cast<f64>(lo) - 1.0) {
                const i64 tf = static_cast<i64>(std::floor(t)) - 1;
                remap.set(l.local_time(FrameIndex{tf}), static_cast<f32>(s0 + static_cast<f64>(tf - lo) * v0));
            }
        }
        if (std::fabs(v1) > 1e-6) {
            const f64 t = std::clamp(static_cast<f64>(hi) + ((v1 > 0 ? sourceLimit : 0.0) - s1) / v1, -maxFrame, maxFrame);
            if (std::isfinite(t) && t > static_cast<f64>(hi) + 1.0) {
                const i64 tf = static_cast<i64>(std::ceil(t)) + 1;
                remap.set(l.local_time(FrameIndex{tf}), static_cast<f32>(s1 + static_cast<f64>(tf - hi) * v1));
            }
        }
    }
    l.timeRemap = std::move(remap);
    l.timeRemapEnabled = true;
    l.tracks.remove_if([](const Track& t) { return t.property == TrackProperty::Speed; });
}

void materialize_clip_time(Layer& l, i64 start, i64 end, f64 sourceLimit) noexcept {
    if (l.speed_track()) { materialize_speed_curve(l, start, end, sourceLimit); return; }
    i64 lo = std::min(start, l.start.value), hi = std::max(end, l.end.value);
    if (sourceLimit >= 0 && l.speed > 0) {
        const f64 slope = l.reversed ? -l.speed : l.speed;
        const f64 origin = l.source_frame(l.start);
        const f64 t0 = l.start.value - origin / slope;
        const f64 t1 = l.start.value + (sourceLimit - origin) / slope;
        // Cover all remaining source handles, so extending a previously trimmed
        // accelerated/reversed clip cannot freeze at the old trim boundary.
        constexpr f64 maxFrame = static_cast<f64>(i64{1} << 40);
        if (std::isfinite(t0) && std::isfinite(t1)) {
            lo = std::min(lo, static_cast<i64>(std::floor(std::clamp(std::min(t0,t1), -maxFrame, maxFrame))) - 1);
            hi = std::max(hi, static_cast<i64>(std::ceil(std::clamp(std::max(t0,t1), -maxFrame, maxFrame))) + 1);
        }
    }
    const f32 a = static_cast<f32>(l.source_frame(FrameIndex{lo})), b = static_cast<f32>(l.source_frame(FrameIndex{hi}));
    l.timeRemap = Track{}; l.timeRemap.property = TrackProperty::TimeRemap;
    l.timeRemap.set(l.local_time(FrameIndex{lo}), a); l.timeRemap.set(l.local_time(FrameIndex{hi}), b);
    l.timeRemapEnabled = true;
}

// Preserve the animation clock AND the sampled source. Offset is also used by
// transform/effect keys, so multiplying it by speed would move those keys.
// Only when the old constant-speed mapping cannot express both clocks do we
// materialize it as the existing, editable time-remap curve.
void trim_clip_copy(Layer& l, i64 start, i64 end, f64 sourceLimit = -1) noexcept {
    const i64 oldStart = l.start.value, oldEnd = l.end.value;
    const i64 ds = start - oldStart;
    if (has_clip_source(l) && !l.timeRemapEnabled &&
        ((l.reversed && (ds != 0 || end != oldEnd)) || (!l.reversed && ds != 0 && (l.speed != 1.0f || l.speed_track())))) {
        materialize_clip_time(l, start, end, sourceLimit);
    }
    l.offset.value += ds;
    l.start = FrameIndex{start};
    l.end = FrameIndex{end};
}
}

bool Engine::edit_clip_time(u64 layerId, u32 operation, i64 amount, u64 previous, u64 next) noexcept {
    constexpr i64 limit = i64{1} << 31;
    if (operation > 5 || amount <= -limit || amount >= limit) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->locked) return false;
    const bool needsPrevious = operation == 3 || operation == 5;
    const bool needsNext = operation == 4 || operation == 5;
    Layer* before = needsPrevious ? comp->layer(LayerId::unpack(previous)) : nullptr;
    Layer* after = needsNext ? comp->layer(LayerId::unpack(next)) : nullptr;
    if (needsPrevious && (!before || before == layer || before->locked || before->end != layer->start)) return false;
    if (needsNext && (!after || after == layer || after == before || after->locked || after->start != layer->end)) return false;
    if (operation == 2 && (!has_clip_source(*layer) || layer->timeRemap.has_expression())) return false;
    const i64 start = layer->start.value, end = layer->end.value;
    const i64 target = operation == 0 ? std::clamp(amount, i64{0}, end - 1)
                     : operation == 1 ? std::max(amount, start + 1) : amount;
    if ((operation == 0 && target == start) || (operation == 1 && target == end) || (operation > 1 && amount == 0)) return false;
    Layer edited = *layer;
    Layer left, right;
    if (before) left = *before;
    if (after) right = *after;
    // A LINHA MAGNÉTICA da camada comanda o ripple, e o modo Edição da
    // composição continua valendo como antes para as camadas que não têm a
    // opção ligada (projetos já montados não mudam de comportamento).
    const bool magnetic = layer->magneticTrack || comp->edit_mode();
    i64 ripple = 0;
    const auto trim = [&](Layer& item, i64 s, i64 e) { trim_clip_copy(item, s, e, clip_source_limit(*project_, *comp, item)); };
    if (operation <= 1) {
        trim(edited, operation == 0 ? target : start, operation == 1 ? target : end);
        if (magnetic) {
            ripple = operation == 0 ? start - target : target - end;
            if (operation == 0) { edited.start.value += ripple; edited.end.value += ripple; }
        }
    } else if (operation == 2) {
        // Change content without moving a single transform/effect key.
        if (!edited.timeRemapEnabled || edited.timeRemap.keys.empty()) {
            materialize_clip_time(edited, start, end, clip_source_limit(*project_, *comp, edited));
        }
        for (auto& key : edited.timeRemap.keys) key.value += static_cast<f32>(amount);
        edited.timeRemap.lastIndex = 0;
    } else if (operation == 3) {
        trim(left, left.start.value, start + amount);
        trim(edited, start + amount, end);
    } else if (operation == 4) {
        trim(edited, start, end + amount);
        trim(right, end + amount, right.end.value);
    } else {
        trim(left, left.start.value, start + amount);
        edited.start.value += amount; edited.end.value += amount;
        trim(right, end + amount, right.end.value);
    }
    const auto valid = [&](const Layer& item) {
        if (item.start.value < 0 || item.end.value <= item.start.value || item.end.value >= limit) return false;
        if (!has_clip_source(item)) return true;
        const f64 maxSource = clip_source_limit(*project_, *comp, item);
        // Missing media cannot supply a reliable source bound; timeline edits
        // must still work so it can be relinked later.
        if (maxSource < 0) return true;
        const auto inside = [&](f64 value) { return std::isfinite(value) && value >= -0.001 && value <= maxSource + 0.001; };
        if (!inside(item.source_frame(item.start)) || !inside(item.source_frame(FrameIndex{item.end.value - 1}))) return false;
        if (operation == 2) {
            const i64 lo = item.local_time(item.start).value, hi = item.local_time(FrameIndex{item.end.value - 1}).value;
            for (const auto& key : item.timeRemap.keys)
                if (key.time.value >= lo && key.time.value <= hi && !inside(key.value)) return false;
        }
        return true;
    };
    if (!valid(edited) || (before && !valid(left)) || (after && !valid(right))) return false;
    // O ripple da LINHA só olha para os vizinhos DAQUELA faixa; o global
    // continua olhando a composição inteira. A proteção do travado segue a
    // mesma regra — é o que impede um trecho travado de ser arrastado junto.
    const bool trackScoped = layer->magneticTrack;
    const u32 track = layer->trackId;
    bool blocked = false;
    if (ripple) comp->layers().for_each([&](LayerId id, const Layer& item) {
        if (id.pack() == layerId || !item.locked || item.start.value < end) return;
        if (trackScoped && item.trackId != track) return;
        blocked = true;
    });
    if (blocked) return false;
    const char* labels[] = {"aparar inicio", "aparar fim", "slip", "roll inicio", "roll fim", "slide"};
    history_.before_mutation(*comp, project_->timeline().current(), labels[operation]);
    *layer = std::move(edited);
    if (before) *before = std::move(left);
    if (after) *after = std::move(right);
    if (ripple) {
        if (trackScoped) comp->shift_track(FrameIndex{end}, ripple, track, LayerId::unpack(layerId));
        else comp->shift_from(FrameIndex{end}, ripple, LayerId::unpack(layerId));
    }
    i64 duration = comp->duration().value;
    comp->layers().for_each([&](LayerId, const Layer& item) { duration = std::max(duration, item.end.value); });
    if (duration != comp->duration().value) { comp->set_duration(FrameIndex{duration}); playback_.configure(comp->fps(), comp->duration()); }
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::reorder_clip(u64 layerId, i64 targetFrame) noexcept {
    constexpr i64 limit = i64{1} << 31;
    if (targetFrame < 0 || targetFrame >= limit) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    Layer* layer = comp->layer(LayerId::unpack(layerId));
    if (!layer || layer->locked || !layer->magneticTrack || !has_clip_source(*layer)) return false;
    const u32 track = layer->trackId;
    const i64 start = layer->start.value;
    const i64 length = layer->end.value - start;
    if (length <= 0 || targetFrame == start) return false;
    // Toda a linha anda junta: um vizinho travado recusaria o movimento no
    // meio do caminho, então a edição inteira é recusada antes de começar.
    bool blocked = false;
    comp->layers().for_each([&](LayerId id, const Layer& other) {
        if (id.pack() != layerId && other.trackId == track && other.locked) blocked = true;
    });
    if (blocked) return false;

    history_.before_mutation(*comp, project_->timeline().current(), "reordenar a linha");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const LayerId id = LayerId::unpack(layerId);
    // 1. Tira o trecho da fita: o buraco que ele deixou fecha sozinho.
    comp->shift_track(FrameIndex{start + length}, -length, track, id);
    // 2. Abre espaço no ponto de soltura. O alvo já está no tempo DEPOIS do
    //    passo 1, e o que estava à frente andou para trás com o fechamento.
    const i64 target = targetFrame >= start + length ? targetFrame - length : targetFrame;
    comp->shift_track(FrameIndex{target}, length, track, id);
    if (Layer* moved = comp->layer(id)) {
        moved->start = FrameIndex{target};
        moved->end = FrameIndex{target + length};
    }
    // 3. A fita encosta de novo: os trechos ficam lado a lado, sem buraco.
    i64 first = target;
    comp->layers().for_each([&](LayerId, const Layer& other) {
        if (other.trackId == track) first = std::min(first, other.start.value);
    });
    comp->close_gaps_in_track(FrameIndex{first}, FrameIndex{target + length}, track);
    comp->rebuild_draw_order();
    i64 duration = comp->duration().value;
    comp->layers().for_each([&](LayerId, const Layer& item) { duration = std::max(duration, item.end.value); });
    if (duration != comp->duration().value) { comp->set_duration(FrameIndex{duration}); playback_.configure(comp->fps(), comp->duration()); }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::move_layer_to_row(u64 layerId, u64 anchorId, i32 mode) noexcept {
    if (mode != 0 && mode != 1) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    const LayerId id = LayerId::unpack(layerId);
    Layer* layer = comp->layer(id);
    if (!layer || layer->locked) return false;
    const LayerId anchor = anchorId ? LayerId::unpack(anchorId) : LayerId{};
    const Layer* a = anchorId ? comp->layer(anchor) : nullptr;
    if ((anchorId && (!a || anchor == id)) || (mode == 1 && (!a || a->trackId == 0))) return false;

    // A ordem SEM a camada que anda (0 = fundo). `move_to` põe a camada num
    // índice desta lista, então todo cálculo de lugar é feito nela.
    std::vector<LayerId> rest;
    rest.reserve(comp->order().size());
    for (u32 i = 0; i < comp->order().size(); ++i)
        if (comp->order().at(i) != id) rest.push_back(comp->order().at(i));
    // A fileira de uma linha fica onde está o trecho MAIS ALTO dela (é assim
    // que as duas timelines montam as fileiras); linha 0 (projeto antigo) é
    // a camada sozinha.
    const auto lineTop = [&](const Layer& of, LayerId self) -> i32 {
        i32 top = -1;
        for (u32 i = 0; i < rest.size(); ++i) {
            const Layer* l = comp->layer(rest[i]);
            if (!l) continue;
            if (of.trackId == 0 ? rest[i] == self : l->trackId == of.trackId) top = static_cast<i32>(i);
        }
        return top;
    };
    // Uma linha só dela: quem dividia a linha com outros ganha um número novo;
    // quem já era sozinho (ou é de projeto antigo, linha 0) fica como está.
    const auto ownTrack = [&]() -> u32 {
        if (layer->trackId == 0) return 0;
        bool shared = false;
        comp->layers().for_each([&](LayerId other, const Layer& l) {
            if (other != id && l.trackId == layer->trackId) shared = true;
        });
        return shared ? comp->new_track_id() : layer->trackId;
    };

    u32 insertAt = 0;
    u32 track = layer->trackId;
    if (mode == 1) {
        const u32 join = a->trackId;
        if (join == layer->trackId) return false;   // já mora nessa linha
        const i32 top = lineTop(*a, anchor);
        if (top < 0) return false;
        // Entra logo ABAIXO do trecho mais alto: a fileira da linha não muda de lugar.
        insertAt = static_cast<u32>(top);
        bool fits = true;
        comp->layers().for_each([&](LayerId other, const Layer& l) {
            if (other != id && l.trackId == join && l.start.value < layer->end.value && layer->start.value < l.end.value)
                fits = false;
        });
        track = fits ? join : ownTrack();
    } else {
        if (a) {
            const i32 top = lineTop(*a, anchor);
            if (top < 0) return false;
            insertAt = static_cast<u32>(top) + 1;
        }
        track = ownTrack();
    }
    const i32 current = comp->order().index_of(id);
    if (current < 0) return false;
    if (static_cast<u32>(current) == insertAt && track == layer->trackId) return false;   // nada muda

    history_.before_mutation(*comp, project_->timeline().current(), "mover para outra fileira");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    layer = comp->layer(id);
    if (!layer) return false;
    const u32 oldTrack = layer->trackId;
    const i64 start = layer->start.value, end = layer->end.value;
    const bool magnetic = layer->magneticTrack;
    layer->trackId = track;
    (void)comp->order().move_to(id, insertAt);
    comp->rebuild_draw_order();
    // Saiu de uma LINHA MAGNÉTICA: a fita dela fica encostada, como no ripple
    // (o trecho que saiu fica no tempo dele; só os vizinhos daquela linha andam).
    if (magnetic && oldTrack != 0 && oldTrack != track)
        comp->close_gaps_in_track(FrameIndex{start}, FrameIndex{end}, oldTrack);
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

i64 Engine::remove_gaps() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return 0;
    // As linhas magnéticas fecham os buracos DELAS; as outras ficam como estão
    // (é o que a pessoa pediu ao desligar a opção). Sem nenhuma linha
    // magnética, vale o comportamento de sempre: a composição inteira.
    std::vector<u32> tracks;
    comp->layers().for_each([&](LayerId, const Layer& l) {
        if (l.magneticTrack && std::find(tracks.begin(), tracks.end(), l.trackId) == tracks.end())
            tracks.push_back(l.trackId);
    });
    const auto close = [&](Composition& c) {
        i64 total = 0;
        if (tracks.empty()) return c.close_gaps(FrameIndex{0}, c.duration());
        for (u32 t : tracks) total += c.close_gaps_in_track(FrameIndex{0}, c.duration(), t);
        return total;
    };
    // Ensaio numa cópia: sem buraco, nada entra no histórico.
    if (close(*comp->clone()) == 0) return 0;
    history_.before_mutation(*comp, project_->timeline().current(), "remover espacos vazios");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const i64 removed = close(*comp);
    project_->mark_dirty();
    request_render();
    return removed;
}

bool Engine::trim_composition(i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || frame <= 0 || frame >= comp->duration().value) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "aparar o projeto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    std::vector<LayerId> gone;
    comp->layers().for_each([&](LayerId id, Layer& l) {
        if (l.start.value >= frame) gone.push_back(id);
        else if (l.end.value > frame) l.end = FrameIndex{frame};
    });
    for (LayerId id : gone) {
        media_.close_layer(id);
        comp->remove_layer(id);
    }
    comp->remove_markers(kMarkerManual, FrameIndex{frame}, FrameIndex{std::numeric_limits<i64>::max()});
    comp->remove_markers(kMarkerBeat, FrameIndex{frame}, FrameIndex{std::numeric_limits<i64>::max()});
    comp->set_duration(FrameIndex{frame});
    comp->rebuild_draw_order();
    if (playback_.current().value >= frame) playback_.seek(FrameIndex{frame - 1}, monotonic_ns());
    project_->mark_dirty();
    request_render();
    return true;
}

void Engine::bump_revision_without_audio_locked() noexcept {
    const u32 before = modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    if (audioRevision_ == before) audioRevision_ = before + 1;
}

bool Engine::toggle_marker(i64 frame) noexcept {
    bool playing = false;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        // Comandos na fila (play/pausa recém-pedidos) valem antes da decisão.
        if (project_) drain_commands_locked();
        playing = playback_.playing();
    }
    if (playing) {
        // Tocando: marca no ouvido, sem alternar e sem tocar no transporte.
        if (mark_beat_live() >= 0) return true;
        std::lock_guard<std::mutex> lock(modelMutex_);
        const Composition* comp = project_ ? current_composition() : nullptr;
        return comp && !comp->markers().empty();
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    const i64 f = std::clamp<i64>(frame, 0, std::max<i64>(0, comp->duration().value - 1));
    const bool had = std::any_of(comp->markers().begin(), comp->markers().end(),
                                 [f](const Marker& m) { return m.frame.value == f; });
    history_.before_mutation(*comp, project_->timeline().current(), had ? "remover marca" : "adicionar marca");
    bump_revision_without_audio_locked();
    if (had) comp->remove_marker_at(FrameIndex{f});
    else comp->put_marker(Marker{FrameIndex{f}, 0xFFF7C34Fu, kMarkerManual, {}});
    project_->mark_dirty();
    return !had;
}

i64 Engine::mark_beat_live() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return -1;
    i64 f = playback_.current().value;
    if (playback_.playing()) {
        // O último quadro desenhado atrasa até um quadro de display; o que o
        // ouvido segue é o relógio mestre (áudio), lido no instante do toque.
        const i64 ns = playback_.clock().media_ns(monotonic_ns());
        f = frame_at(TickNs{ns}, comp->fps()).value;
        const i64 duration = comp->duration().value;
        if (playback_.loop() && duration > 0) f = ((f % duration) + duration) % duration;
    }
    f = std::clamp<i64>(f, 0, std::max<i64>(0, comp->duration().value - 1));
    if (std::any_of(comp->markers().begin(), comp->markers().end(),
                    [f](const Marker& m) { return m.frame.value == f; })) return -1;
    history_.before_mutation(*comp, project_->timeline().current(), "marcar batida");
    bump_revision_without_audio_locked();
    comp->put_marker(Marker{FrameIndex{f}, 0xFFF7C34Fu, kMarkerManual, {}});
    project_->mark_dirty();
    return f;
}

bool Engine::move_marker(i64 from, i64 to) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    auto it = std::find_if(comp->markers().begin(), comp->markers().end(),
                           [from](const Marker& m) { return m.frame.value == from; });
    if (it == comp->markers().end()) return false;
    const i64 target = std::clamp<i64>(to, 0, std::max<i64>(0, comp->duration().value - 1));
    if (target == from) return true;
    // A drag must not silently replace another marker (including its label/color).
    if (std::any_of(comp->markers().begin(), comp->markers().end(),
                    [target](const Marker& marker) { return marker.frame.value == target; })) return false;
    Marker m = *it;
    history_.before_mutation(*comp, project_->timeline().current(), "mover marca");
    bump_revision_without_audio_locked();
    comp->remove_marker_at(FrameIndex{from});
    m.frame = FrameIndex{target};
    comp->put_marker(std::move(m));
    project_->mark_dirty();
    return true;
}

bool Engine::edit_marker(i64 from, i64 to, u32 color, const std::string& label) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || to < 0 || to >= comp->duration().value || label.size() > 1024) return false;
    const auto& markers = comp->markers();
    auto source = std::find_if(markers.begin(), markers.end(),
                              [from](const Marker& m) { return m.frame.value == from; });
    if (from >= 0 && source == markers.end()) return false;
    if (std::any_of(markers.begin(), markers.end(), [from, to](const Marker& m) {
            return m.frame.value == to && m.frame.value != from;
        })) return false;
    color |= 0xFF000000u;
    if (source != markers.end() && from == to && source->color == color && source->label == label) return true;
    const u32 kind = source == markers.end() ? kMarkerManual : source->kind;
    history_.before_mutation(*comp, project_->timeline().current(), from < 0 ? "adicionar marca" : "editar marca");
    if (from >= 0) comp->remove_marker_at(FrameIndex{from});
    comp->put_marker(Marker{FrameIndex{to}, color, kind, label});
    bump_revision_without_audio_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::delete_marker(i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || std::none_of(comp->markers().begin(), comp->markers().end(),
                             [frame](const Marker& m) { return m.frame.value == frame; })) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "remover marca");
    comp->remove_marker_at(FrameIndex{frame});
    bump_revision_without_audio_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

std::string Engine::marker_label(i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return {};
    const auto& markers = comp->markers();
    const auto found = std::lower_bound(markers.begin(), markers.end(), frame,
                                       [](const Marker& marker, i64 f) { return marker.frame.value < f; });
    if (found != markers.end() && found->frame.value == frame) return found->label;
    return {};
}

u32 Engine::query_markers(i64* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return 0;
    const auto& ms = comp->markers();
    for (u32 i = 0; i < ms.size() && i < capacity && out; ++i) {
        out[i * 3] = ms[i].frame.value;
        out[i * 3 + 1] = static_cast<i64>(ms[i].color);
        out[i * 3 + 2] = static_cast<i64>(ms[i].kind);
    }
    return static_cast<u32>(ms.size());
}

Result<u32> Engine::detect_beats(u64 layerId, f64* bpmOut) noexcept {
    // 1. O que decodificar (sob o lock, rápido).
    u64 key = 0;
    audio::AudioAssetRef ref;
    f64 fps = 30.0, rate = 1.0, atStart = 0.0;
    bool reversed = false;
    i64 start = 0, end = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = project_ ? current_composition() : nullptr;
        if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
        const Layer* l = comp->layer(LayerId::unpack(layerId));
        if (!l || (l->kind != LayerKind::Video && l->kind != LayerKind::Audio)) return Status{Errc::InvalidArgument, "camada sem som"};
        const Asset* a = project_->asset(l->source);
        if (!a || !a->has_audio()) return Status{Errc::InvalidArgument, "camada sem som"};
        if (l->speed <= 0.0f) return Status{Errc::InvalidArgument, "quadro congelado"};
        key = l->source.pack();
        fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        rate = l->speed;
        reversed = l->reversed;
        atStart = l->source_frame(l->start);
        start = l->start.value;
        end = l->end.value;
        const i64 len = a->audio.sampleCount.value > 0 && a->audio.sampleRate > 0
                      ? a->audio.sampleCount.value * static_cast<i64>(audio::kMixRate) / a->audio.sampleRate
                      : audio::frame_to_sample(a->duration.value, a->timebaseFps > 0.0 ? a->timebaseFps : fps);
        ref = audio::AudioAssetRef{resolve_asset_path(a->sourcePath), len};
    }
    VideoSourceFactory* factory = config_.mediaFactory;
    if (!factory) return Status{Errc::InvalidState, "sem decodificador"};
    // 2. Só o trecho da fonte que a camada usa, em mono.
    const f64 srcA = atStart, srcB = atStart + (reversed ? -1.0 : 1.0) * static_cast<f64>(end - start) * rate;
    const i64 s0 = std::max<i64>(0, static_cast<i64>(std::floor(std::min(srcA, srcB) * audio::kMixRate / fps)));
    const i64 s1 = std::min<i64>(ref.durationSamples, static_cast<i64>(std::ceil(std::max(srcA, srcB) * audio::kMixRate / fps)));
    if (s1 - s0 < static_cast<i64>(audio::kMixRate) * 3) return Status{Errc::InvalidArgument, "trecho curto demais"};
    audio::AudioBlockCache blocks(factory, 2ull << 20, false);
    blocks.register_asset(key, ref);
    std::vector<f32> mono(static_cast<usize>(s1 - s0), 0.0f);
    for (i64 b = s0 / audio::kBlockFrames; b * audio::kBlockFrames < s1; ++b) {
        auto blk = blocks.fetch(key, b);
        if (!blk) {
            if (b == s0 / audio::kBlockFrames) return Status{Errc::IoError, "audio ilegivel"};
            break;
        }
        const i64 base = b * audio::kBlockFrames;
        for (i64 i = std::max(base, s0); i < std::min(base + static_cast<i64>(audio::kBlockFrames), s1); ++i) {
            const usize k = static_cast<usize>(i - base) * 2;
            mono[static_cast<usize>(i - s0)] = 0.5f * (blk->pcm[k] + blk->pcm[k + 1]);
        }
    }
    const u64 t0 = monotonic_ns();
    const audio::BeatResult br = audio::detect_beats(mono.data(), mono.size());
    if (bpmOut) *bpmOut = br.bpm;
    AUREA_LOG_INFO("batidas: %zu a %.1f BPM em %.1f s de audio (%.0f ms)", br.beats.size(), br.bpm,
                   static_cast<f64>(mono.size()) / audio::kMixRate, static_cast<f64>(monotonic_ns() - t0) / 1e6);
    // 3. Segundos da fonte → frames da timeline (mesma conta do vídeo/mixer).
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "projeto fechado"};
    history_.before_mutation(*comp, project_->timeline().current(), "detectar batidas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    comp->remove_markers(kMarkerBeat, FrameIndex{start}, FrameIndex{end});
    u32 placed = 0;
    for (f64 sec : br.beats) {
        const f64 srcFrame = (static_cast<f64>(s0) / audio::kMixRate + sec) * fps;
        const f64 elapsed = reversed ? (atStart - srcFrame) / rate : (srcFrame - atStart) / rate;
        const i64 f = start + static_cast<i64>(std::llround(elapsed));
        if (f < start || f >= end) continue;
        const bool taken = std::any_of(comp->markers().begin(), comp->markers().end(),
                                       [f](const Marker& m) { return m.frame.value == f; });
        if (taken) continue;   // a marca da pessoa manda
        comp->put_marker(Marker{FrameIndex{f}, 0xFF4DB7FFu, kMarkerBeat, {}});
        ++placed;
    }
    project_->mark_dirty();
    return placed;
}

Result<u64> Engine::add_shape(u32 preset) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    history_.before_mutation(*comp, project_->timeline().current(), "adicionar forma");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    // Ladrilhos da aba Forma, na ordem da grade (A.01).
    struct Preset { const char* name; u32 type; f32 corner; f32 points; f32 inner; f32 aspect; };
    static constexpr Preset kPresets[] = {
        {"Círculo", 1, 0, 5, 0.5f, 1.0f},          {"Quadrado arredondado", 0, 0.18f, 5, 0.5f, 1.0f},
        {"Cruz", 5, 0, 5, 0.34f, 1.0f},            {"Anel", 6, 0, 5, 0.6f, 1.0f},
        {"Triângulo", 3, 0, 3, 0.5f, 1.0f},        {"Fatia", 7, 0, 5, 0.5f, 1.0f},
        {"Hexágono", 3, 0, 6, 0.5f, 1.0f},         {"Flor", 8, 0, 6, 0.5f, 1.0f},
        {"Seta", 9, 0, 5, 0.5f, 1.6f},             {"Hexágono (pontos)", 3, 0, 6, 0.5f, 1.0f},
        {"Quadrado", 0, 0, 5, 0.5f, 1.0f},         {"Estrela", 4, 0, 5, 0.45f, 1.0f},
        {"Cápsula", 0, 0.5f, 5, 0.5f, 3.0f},       {"Retângulo arredondado", 0, 0.12f, 5, 0.5f, 1.0f},
        {"Triângulo retângulo", 10, 0, 3, 0.5f, 1.0f},
        // Formas trazidas do app antigo (índices novos no fim: a grade já gravada não muda).
        {"Octógono", 3, 0, 8, 0.5f, 1.0f},        {"Pentágono", 3, 0, 5, 0.5f, 1.0f},
        {"Trapézio", 12, 0, 5, 0.6f, 1.3f},       {"Paralelogramo", 13, 0, 5, 0.5f, 1.4f},
        {"Estrela de 4 pontas", 4, 0, 4, 0.4f, 1.0f}, {"Estrela de 6 pontas", 4, 0, 6, 0.55f, 1.0f},
        {"Engrenagem", 14, 0, 12, 0.3f, 1.0f},     {"Seta dupla", 15, 0, 5, 0.22f, 1.6f},
    };
    const Preset& pr = kPresets[std::min<u32>(preset, static_cast<u32>(std::size(kPresets) - 1))];
    const LayerId lid = comp->add_layer(LayerKind::Shape, pr.name);
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    const f32 base = 0.3f * static_cast<f32>(std::min(comp->width(), comp->height()));
    ShapeData& sh = l->shape;
    sh.shapeType = pr.type;
    sh.bounds = Rect{0.0f, 0.0f, std::round(base * pr.aspect), std::round(pr.aspect > 1.5f ? base : base)};
    if (pr.aspect == 3.0f) sh.bounds.h = std::round(base * 0.9f);
    sh.cornerRadius = pr.corner * std::min(sh.bounds.w, sh.bounds.h);
    sh.points = pr.points;
    sh.innerRadius = pr.inner;
    sh.fillColor = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    sh.filled = true;
    // O cursor pode estar DEPOIS do fim da composição (a timeline não trava
    // mais no fim): a camada nasce onde ele está e a duração acompanha, senão
    // ela nasceria fora do projeto e não apareceria.
    const i64 t = std::max<i64>(0, playback_.current().value);
    l->start = FrameIndex{t};
    l->end = FrameIndex{std::max<i64>(t + 1, comp->duration().value)};
    if (l->end.value > comp->duration().value) {
        comp->set_duration(l->end);
        playback_.configure(comp->fps(), comp->duration());
    }
    l->transform.anchor = Vec3{sh.bounds.w * 0.5f, sh.bounds.h * 0.5f, 0.0f};
    l->transform.position = Vec3{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

Result<u64> Engine::import_image(const u8* rgba, u32 width, u32 height, const char* name,
                                 const char* sourcePath) noexcept {
    if (!rgba || width == 0 || height == 0) return Status{Errc::InvalidArgument, "imagem vazia"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    history_.before_mutation(*comp, project_->timeline().current(), "importar imagem");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);

    Asset asset;
    asset.kind = AssetKind::Image;
    asset.name = name ? name : "Imagem";
    asset.sourcePath = sourcePath ? sourcePath : "";
    asset.originalFilename = asset.name;
    asset.video.width = width;    // tamanho da mídia: detalhe da camada, hit-test do palco
    asset.video.height = height;
    const AssetId assetId = project_->add_asset(std::move(asset));

    ImagePixels px;
    px.width = width;
    px.height = height;
    px.rgba.assign(rgba, rgba + static_cast<usize>(width) * height * 4);
    images_[assetId.pack()] = std::move(px);

    const LayerId lid = comp->add_layer(LayerKind::Image, name ? std::string(name) : std::string());
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->source = assetId;
    l->transform.anchor = Vec3{static_cast<f32>(width) * 0.5f, static_cast<f32>(height) * 0.5f, 0.0f};
    l->transform.position = Vec3{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    const f32 fit = std::min(static_cast<f32>(comp->width()) / static_cast<f32>(width),
                             static_cast<f32>(comp->height()) / static_cast<f32>(height));
    l->transform.scale = Vec3{fit, fit, 1.0f};
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

// -----------------------------------------------------------------------------
// Modelos 3D
// -----------------------------------------------------------------------------
namespace {

std::string asset_path_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

// A docs: suffix is a portable relative path, never an absolute path/drive,
// alternate data stream, or a traversal. Accept old Windows separators too.
std::string safe_asset_relative(std::string relative) {
    if (relative.empty() || relative.find('\0') != std::string::npos || relative.find(':') != std::string::npos) return {};
    std::replace(relative.begin(), relative.end(), '\\', '/');
    usize first = 0;
    while (first <= relative.size()) {
        const usize slash = relative.find('/', first);
        const usize end = slash == std::string::npos ? relative.size() : slash;
        const std::string part = relative.substr(first, end - first);
        if (part.empty() || part == "." || part == "..") return {};
        if (slash == std::string::npos) break;
        first = slash + 1;
    }
    return relative;
}

// Canonical containment also rejects an otherwise valid suffix whose symlink
// leaves Documents. error_code overloads keep missing media a normal failure.
std::string document_asset_path(const std::string& docs, const std::string& relative, bool mustExist) {
    if (docs.empty() || relative.empty()) return {};
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path base = fs::weakly_canonical(fs::u8path(docs), error);
    if (error || base.empty()) return {};
    const fs::path candidate = fs::weakly_canonical(base / fs::u8path(relative), error);
    if (error || candidate.empty()) return {};
    if (safe_asset_relative(asset_path_utf8(candidate.lexically_relative(base))).empty()) return {};
    if (mustExist && (!fs::is_regular_file(candidate, error) || error)) return {};
    return asset_path_utf8(candidate);
}

} // namespace

std::string Engine::store_asset_path(const std::string& absolute) const {
    const std::string& docs = config_.documentsDirectory;
    if (docs.empty() || absolute.empty()) return absolute;
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path base = fs::weakly_canonical(fs::u8path(docs), error);
    if (error || base.empty()) return absolute;
    const fs::path candidate = fs::weakly_canonical(fs::u8path(absolute), error);
    if (error || candidate.empty()) return absolute;
    const std::string relative = safe_asset_relative(asset_path_utf8(candidate.lexically_relative(base)));
    if (!relative.empty()) return "docs:" + relative;
    return absolute;
}

std::string Engine::audio_path_resolver(void* self, const std::string& stored) {
    return static_cast<Engine*>(self)->resolve_asset_path(stored);
}

void Engine::sync_audio_locked(const Composition& comp) noexcept {
    const u32 rev = modelRevision_.load(std::memory_order_acquire);
    if (rev != audioRevision_ || &comp != audioComp_) {
        audioRevision_ = rev;
        audioComp_ = &comp;
        if (const Layer* raw = comp.layer(rawPlaybackLayer_)) {
            auto snap = std::make_shared<audio::AudioMixSnapshot>();
            if (const Asset* asset = project_->asset(raw->source); asset && asset->has_audio()) {
                audio::AudioClip clip;
                clip.asset = raw->source.pack();
                clip.end = clip.sourceLength = audio::frame_to_sample(asset->video.frameCount.value, asset->video.fps);
                clip.fadeTo = clip.end;
                snap->endSample = clip.end;
                snap->clips.push_back(clip);
                audio_.cache()->register_asset(clip.asset, audio::AudioAssetRef{resolve_asset_path(asset->sourcePath), clip.end});
            }
            audio_.set_snapshot(std::move(snap));
        } else audio_.set_snapshot(audio::build_snapshot(comp, *project_, audio_.cache(), &Engine::audio_path_resolver, this));
    }
    // Audio remains master at every supported transport rate.
    const bool want = playback_.playing()
                   && !exportActive_.load(std::memory_order_acquire);
    if (want) {
        if (!audio_.playing() || playback_.generation() != audioGeneration_ || std::fabs(audio_.rate() - playback_.speed()) > 1e-6) {
            audio_.play(playback_.current_ns(), playback_.speed());
            audioGeneration_ = playback_.generation();
        }
        return;
    }
    if (audio_.playing()) audio_.stop();
    if (playback_.generation() != audioGeneration_) {
        // Parado num ponto novo: o som dali já começa a decodificar, e o play
        // seguinte sai sem buraco.
        audioGeneration_ = playback_.generation();
        audio_.prefetch(playback_.current_ns());
    }
}

std::string Engine::resolve_asset_path(const std::string& stored) const {
    if (stored.rfind("docs:", 0) == 0) {
        return document_asset_path(config_.documentsDirectory, safe_asset_relative(stored.substr(5)), false);
    }
    // Old Android HDRI imports stored the application's private files path.
    // Rebase only these exact package prefixes, with an existing companion;
    // unknown packages and external/media-provider paths retain their meaning.
    constexpr const char* legacyRoots[] = {
        "/data/user/0/com.aurea.aurea/files/", "/data/data/com.aurea.aurea/files/"
    };
    for (const char* prefix : legacyRoots) {
        if (stored.rfind(prefix, 0) != 0) continue;
        const std::string relative = safe_asset_relative(stored.substr(std::strlen(prefix)));
        if (relative.empty()) return {};
        if (config_.documentsDirectory.empty()) return stored;
        // Android's project root is files/projetos, but older model imports
        // were stored in its sibling files/modelos. Resolve that exact owned
        // directory before trying a portable companion inside Documents.
        const auto docs = std::filesystem::u8path(config_.documentsDirectory).lexically_normal();
        if (docs.filename() == "projetos" && relative.rfind("modelos/", 0) == 0) {
            const auto legacy = document_asset_path(asset_path_utf8(docs.parent_path()), relative, true);
            if (!legacy.empty()) return legacy;
        }
        // No companion (or a symlink leaving Documents) stays missing; never
        // fall back to a different application's container or a basename scan.
        return document_asset_path(config_.documentsDirectory, relative, true);
    }
    return stored;
}

Result<u64> Engine::import_model(const ModelImport& request, scene3d::ImportProgress* progress,
                                 std::string* detail) noexcept {
    if (request.path.empty()) return Status{Errc::InvalidArgument, "caminho vazio"};
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    }
    // Parse, validação e otimização FORA do lock: o preview continua rodando.
    scene3d::ImportOptions options;
    options.maxTextureSize = model_texture_cap();
    scene3d::ImportResult r = scene3d::import_scene_file(request.path, options, progress);
    if (!r.ok()) {
        if (detail) *detail = r.detail;
        const Errc code = r.error == scene3d::ImportError::Cancelled ? Errc::Cancelled
                        : r.error == scene3d::ImportError::FileNotFound ? Errc::NotFound
                        : r.error == scene3d::ImportError::OutOfMemory ? Errc::OutOfMemory
                        : r.error == scene3d::ImportError::UnsupportedCompression
                          || r.error == scene3d::ImportError::UnsupportedFeature ? Errc::UnsupportedFeature
                        : Errc::AssetCorrupted;
        return Status{code, scene3d::to_string(r.error)};
    }
    std::shared_ptr<const scene3d::SceneAsset> scene(std::move(r.asset));
    if (detail) {
        detail->clear();
        for (const std::string& w : scene->warnings) *detail += w + "\n";
    }
    if (progress) progress->phase.store(scene3d::ImportPhase::Complete, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "projeto fechado durante o import"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    history_.before_mutation(*comp, project_->timeline().current(), "importar modelo 3D");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);

    Asset asset;
    asset.kind = AssetKind::Model3D;
    asset.name = request.displayName.empty() ? scene->sourceName : request.displayName;
    asset.sourcePath = store_asset_path(request.path);
    asset.originalFilename = scene->sourceName;
    asset.model.meshCount = scene->stats.meshes;
    asset.model.materialCount = scene->stats.materials;
    asset.model.animationCount = scene->stats.animations;
    asset.model.triangleCount = scene->stats.triangles;
    {
        u32 levels = 1;
        for (const scene3d::Mesh& m : scene->meshes) for (const scene3d::Primitive& p : m.primitives)
            levels = std::max<u32>(levels, static_cast<u32>(p.lods.size()) + 1u);
        asset.model.lodCount = levels;
    }
    asset.model.hasSkeleton = scene->stats.skins > 0;
    asset.model.hasMorphTargets = scene->stats.morphTargets > 0;
    for (const scene3d::Animation& a : scene->animations) asset.model.animationNames.push_back(a.name);
    const AssetId assetId = project_->add_asset(std::move(asset));
    models_[assetId.pack()] = scene;

    const LayerId lid = comp->add_layer(LayerKind::Model3D, request.displayName.empty() ? scene->sourceName
                                                                                       : request.displayName);
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->threeD = true;
    l->model.scene = assetId;
    // Enquadramento: a silhueta vista de frente (largura/altura; a
    // profundidade pesa menos) ocupa ~55% do lado menor da composição,
    // centrada. A escala da layer fica em 100% para o usuário.
    const Vec3 ext = scene->bounds.extent();
    const f32 maxExt = std::max({ext.x, ext.y, ext.z * 0.6f, 1e-6f});
    l->model.unitScale = 0.55f * static_cast<f32>(std::min(comp->width(), comp->height())) / maxExt;
    // Com animação no arquivo, a primeira toca (no relógio da timeline) — é o
    // que se espera ao importar um personagem.
    l->model.animationClip = scene->animations.empty() ? -1 : 0;
    l->model.pivot = scene->bounds.center();
    l->transform.position = Vec3{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    l->transform.scale = Vec3{1.0f, 1.0f, 1.0f};
    project_->mark_dirty();
    request_render();
    AUREA_LOG_INFO("modelo 3D importado: %s (%u triangulos, escala %.3f px/m)", scene->sourceName.c_str(),
                   scene->stats.triangles, l->model.unitScale);
    return lid.pack();
}

namespace {
AssetId add_model_asset(Project& project, const scene3d::SceneAsset& scene, std::string name, std::string sourcePath) {
    Asset asset;
    asset.kind = AssetKind::Model3D;
    asset.name = std::move(name);
    asset.sourcePath = std::move(sourcePath);
    asset.originalFilename = scene.sourceName;
    asset.model.meshCount = scene.stats.meshes;
    asset.model.materialCount = scene.stats.materials;
    asset.model.triangleCount = scene.stats.triangles;
    asset.model.lodCount = 1;
    return project.add_asset(std::move(asset));
}
} // namespace

std::vector<text::FontEntry> Engine::list_fonts() noexcept { return text::FontManager::instance().list(); }

Result<text::FontEntry> Engine::import_font(const char* path) noexcept {
    if (!path || !*path) return Status{Errc::InvalidArgument, "caminho vazio"};
    const text::FontEntry* e = text::FontManager::instance().add_file(path, true);
    if (!e) return Status{Errc::UnsupportedFormat, "nao e uma fonte TTF/OTF legivel"};
    return *e;
}

bool Engine::set_text_font(u64 layerId, const std::string& family, u32 weight, bool italic, const std::string& path) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "fonte do texto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->text.fontFamily = family;
    l->text.fontWeight = static_cast<u16>(std::clamp<u32>(weight, 100, 1000));
    l->text.fontItalic = italic;
    // Importada: o arquivo vai relativo ao projeto (abre em outro aparelho).
    l->text.fontPath = path.empty() ? std::string{} : store_asset_path(path);
    recenter_text(*l);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_text_style(u64 layerId, const f32* v, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text || !v || count < 18) return false;
    for (u32 i = 0; i < std::min(count, 20u); ++i) if (!std::isfinite(v[i])) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "estilo do texto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    TextData& t = l->text;
    t.boxMode = static_cast<u32>(std::clamp(v[0], 0.0f, 3.0f));
    t.autoSize = t.boxMode == 0;
    t.box.w = std::clamp(v[1], 10.0f, 20000.0f);
    t.box.h = std::clamp(v[2], 10.0f, 20000.0f);
    t.background = v[3] > 0.5f;
    t.backgroundColor = Vec4{v[4], v[5], v[6], v[7]};
    t.backgroundPadding = std::clamp(v[8], 0.0f, 500.0f);
    t.backgroundRadius = std::clamp(v[9], 0.0f, 500.0f);
    t.shadow = v[10] > 0.5f;
    t.shadowColor = Vec4{v[11], v[12], v[13], v[14]};
    t.shadowOffset = Vec2{std::clamp(v[15], -500.0f, 500.0f), std::clamp(v[16], -500.0f, 500.0f)};
    t.shadowBlur = std::clamp(v[17], 0.0f, 200.0f);
    if (count >= 20) {
        t.lineHeight = std::clamp(v[18], 0.1f, 10.0f);
        t.tracking = std::clamp(v[19], -1000.0f, 1000.0f);
    }
    recenter_text(*l);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_text_style(u64 layerId, f32* v, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text || !v || capacity < 18) return false;
    const TextData& t = l->text;
    const f32 o[18] = {static_cast<f32>(t.boxMode), t.box.w, t.box.h, t.background ? 1.0f : 0.0f, t.backgroundColor.x, t.backgroundColor.y,
                       t.backgroundColor.z, t.backgroundColor.w, t.backgroundPadding, t.backgroundRadius, t.shadow ? 1.0f : 0.0f,
                       t.shadowColor.x, t.shadowColor.y, t.shadowColor.z, t.shadowColor.w, t.shadowOffset.x, t.shadowOffset.y, t.shadowBlur};
    std::copy(o, o + 18, v);
    if (capacity >= 20) { v[18] = t.lineHeight; v[19] = t.tracking; }
    return true;
}

namespace {
/// Tira [s, e) dos trechos existentes (corta os que atravessam a borda).
void cut_spans(std::vector<TextSpan>& spans, u32 s, u32 e) {
    std::vector<TextSpan> out;
    for (const TextSpan& sp : spans) {
        if (sp.end <= s || sp.start >= e) { out.push_back(sp); continue; }
        if (sp.start < s) { TextSpan a = sp; a.end = s; out.push_back(a); }
        if (sp.end > e) { TextSpan b = sp; b.start = e; out.push_back(b); }
    }
    spans.swap(out);
}
} // namespace

bool Engine::set_text_span(u64 layerId, u32 start, u32 end, bool hasColor, Vec4 color, u32 weight, f32 scale) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text || end <= start) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "estilo do trecho");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    // O mesmo trecho de novo: junta (cor + negrito + tamanho), não substitui.
    for (const TextSpan& old : l->text.spans) {
        if (old.start != start || old.end != end) continue;
        if (!hasColor && old.hasColor) { hasColor = true; color = old.color; }
        if (weight == 0) weight = old.weight;
        if (scale == 1.0f) scale = old.scale;
    }
    cut_spans(l->text.spans, start, end);
    TextSpan sp;
    sp.start = start;
    sp.end = end;
    sp.hasColor = hasColor;
    sp.color = color;
    sp.weight = static_cast<u16>(std::min<u32>(weight, 1000));
    sp.scale = std::clamp(scale, 0.1f, 10.0f);
    l->text.spans.push_back(sp);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::clear_text_spans(u64 layerId, u32 start, u32 end) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "limpar estilo do trecho");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    cut_spans(l->text.spans, start, end);
    project_->mark_dirty();
    request_render();
    return true;
}

namespace {
/// Posição do parâmetro animável no vetor de 40 floats (−1 = sem posição).
i32 text_param_slot(u32 p) noexcept {
    if (p <= text::kSelEaseLow) return 7 + static_cast<i32>(p);
    if (p == text::kWiggleRate) return 13;
    if (p >= text::kPosX && p <= text::kCharOffset) return 14 + static_cast<i32>(p - text::kPosX);
    return -1;
}
f32* text_param_ref(TextAnimator& a, u32 p) noexcept {
    switch (p) {
        case text::kSelStart: return &a.selector.start;
        case text::kSelEnd: return &a.selector.end;
        case text::kSelOffset: return &a.selector.offset;
        case text::kSelAmount: return &a.selector.amount;
        case text::kSelEaseHigh: return &a.selector.easeHigh;
        case text::kSelEaseLow: return &a.selector.easeLow;
        case text::kWiggleRate: return &a.selector.wiggleRate;
        case text::kPosX: return &a.position.x;
        case text::kPosY: return &a.position.y;
        case text::kPosZ: return &a.position.z;
        case text::kScaleX: return &a.scale.x;
        case text::kScaleY: return &a.scale.y;
        case text::kRotX: return &a.rotation.x;
        case text::kRotY: return &a.rotation.y;
        case text::kRotZ: return &a.rotation.z;
        case text::kOpacity: return &a.opacity;
        case text::kTracking: return &a.tracking;
        case text::kBlur: return &a.blur;
        case text::kSkew: return &a.skew;
        case text::kStrokeWidth: return &a.strokeWidth;
        case text::kCharOffset: return &a.charOffset;
        case text::kFillR: return &a.fill.x;
        case text::kFillG: return &a.fill.y;
        case text::kFillB: return &a.fill.z;
        case text::kStrokeR: return &a.stroke.x;
        case text::kStrokeG: return &a.stroke.y;
        case text::kStrokeB: return &a.stroke.z;
        default: return nullptr;
    }
}
f32 clamp_text_param(u32 p, f32 v) noexcept {
    switch (p) {
        case text::kFillR: case text::kFillG: case text::kFillB:
        case text::kStrokeR: case text::kStrokeG: case text::kStrokeB: return std::clamp(v, 0.0f, 1.0f);
        case text::kSelStart: case text::kSelEnd: return std::clamp(v, 0.0f, 100.0f);
        case text::kSelOffset: return std::clamp(v, -1000.0f, 1000.0f);
        case text::kSelAmount: return std::clamp(v, -100.0f, 100.0f);
        case text::kSelEaseHigh: case text::kSelEaseLow: return std::clamp(v, -100.0f, 100.0f);
        case text::kWiggleRate: return std::clamp(v, 0.0f, 60.0f);
        case text::kOpacity: return std::clamp(v, 0.0f, 100.0f);
        case text::kScaleX: case text::kScaleY: return std::clamp(v, -2000.0f, 2000.0f);
        case text::kBlur: return std::clamp(v, 0.0f, 200.0f);
        case text::kSkew: return std::clamp(v, -80.0f, 80.0f);
        case text::kStrokeWidth: return std::clamp(v, -50.0f, 50.0f);
        default: return std::clamp(v, -100000.0f, 100000.0f);
    }
}
} // namespace

u32 Engine::query_text_animators(u64 layerId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !supports_text_animation(*l)) return 0;
    const FrameIndex local = l->local_time(playback_.current());
    const u32 n = static_cast<u32>(l->text.animators.size());
    for (u32 i = 0; i < n && out && (i + 1) * kTextAnimFloats <= capacity; ++i) {
        TextAnimator a = l->text.animators[i];
        f32* v = out + i * kTextAnimFloats;
        u32 animSel = 0, animProp = 0, keySel = 0, keyProp = 0;
        for (u32 p = 0; p <= text::kStrokeB; ++p) {
            f32* r = text_param_ref(a, p);
            if (!r) continue;
            const Track* tr = l->tracks.find(TrackProperty::TextAnimParam, i, p);
            if (!tr || !tr->driven()) continue;
            *r = tr->value_or(local, *r);   // keyframes e/ou expressão
            if (tr->keys.empty()) continue;  // só expressão: sem losango
            const bool k = tr->find_exact(local) != kInvalidIndex;
            if (p < 10) { animSel |= 1u << p; if (k) keySel |= 1u << p; }
            else { animProp |= 1u << (p - 10); if (k) keyProp |= 1u << (p - 10); }
        }
        const TextSelector& s = a.selector;
        const f32 o[kTextAnimFloats] = {a.enabled ? 1.0f : 0.0f, static_cast<f32>(a.props), static_cast<f32>(s.basedOn), static_cast<f32>(s.type),
            static_cast<f32>(s.shape), s.randomOrder ? 1.0f : 0.0f, static_cast<f32>(s.seed), s.start, s.end, s.offset, s.amount, s.easeHigh,
            s.easeLow, s.wiggleRate, a.position.x, a.position.y, a.position.z, a.scale.x, a.scale.y, a.rotation.x, a.rotation.y,
            a.rotation.z, a.opacity, a.tracking, a.blur, a.skew, a.strokeWidth, a.charOffset, a.fill.x, a.fill.y, a.fill.z, a.fill.w,
            a.stroke.x, a.stroke.y, a.stroke.z, a.stroke.w, static_cast<f32>(animSel), static_cast<f32>(animProp), static_cast<f32>(keySel),
            static_cast<f32>(keyProp)};
        std::copy(o, o + kTextAnimFloats, v);
    }
    return n;
}

i32 Engine::add_text_animator(u64 layerId, u32 props) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || !supports_text_animation(*l) || l->text.animators.size() >= 64) return -1;
    Layer prepared = *l;
    if (prepared.kind == LayerKind::Model3D && !ensure_text3d_layout(prepared).ok()) return -1;
    history_.before_mutation(*comp, project_->timeline().current(), "novo animador de texto");
    *l = std::move(prepared);
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    TextAnimator a;
    a.name = "Animador " + std::to_string(l->text.animators.size() + 1);
    a.props = props & 0x7FFu;
    const u32 index = static_cast<u32>(l->text.animators.size());
    // The default "Add animation" action must create motion, rather than an
    // opacity=100 animator without keys (which can never change the image).
    if (a.props == kTextPropOpacity) {
        a.opacity = 0;
        a.selector.shape = 0;
        const i64 at = l->contains_time(playback_.current()) ? playback_.current().value : l->start.value;
        const i64 begin = std::min(at, std::max(l->start.value, l->end.value - 2));
        const i64 remaining = std::max<i64>(1, l->end.value - 1 - begin);
        const i64 duration = std::min(remaining, std::max<i64>(1, static_cast<i64>(std::lround(comp->fps()))));
        const FrameIndex local = l->local_time(FrameIndex{begin});
        Track& selector = l->tracks.get_or_create(TrackProperty::TextAnimParam, index, text::kSelStart);
        if (begin > l->start.value) (void)selector.set(FrameIndex{local.value - 1}, 100.0f, Interpolation::Hold);
        (void)selector.set(local, 0.0f, Interpolation::EaseInOut);
        (void)selector.set(FrameIndex{local.value + duration}, 100.0f, Interpolation::Linear);
    }
    l->text.animators.push_back(a);
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(l->text.animators.size() - 1);
}

bool Engine::remove_text_animator(u64 layerId, u32 index) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || !supports_text_animation(*l) || index >= l->text.animators.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "remover animador de texto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->text.animators.erase(l->text.animators.begin() + index);
    // As trilhas do removido saem; as dos seguintes descem um índice.
    l->tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::TextAnimParam && t.effectIndex == index; });
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        Track& t = l->tracks.at(i);
        if (t.property == TrackProperty::TextAnimParam && t.effectIndex > index) --t.effectIndex;
    }
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::duplicate_text_animator(u64 layerId, u32 index) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || !supports_text_animation(*l) || index >= l->text.animators.size() || l->text.animators.size() >= 64) return -1;
    const u32 target = index + 1;
    TextAnimator copy = l->text.animators[index];
    std::vector<Track> tracks;
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        const Track& t = l->tracks.at(i);
        if (t.property == TrackProperty::TextAnimParam && t.effectIndex == index) {
            tracks.push_back(t);
            tracks.back().effectIndex = target;
        }
    }
    history_.before_mutation(*comp, project_->timeline().current(), "duplicar animador de texto");
    l->text.animators.insert(l->text.animators.begin() + target, std::move(copy));
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        Track& t = l->tracks.at(i);
        if (t.property == TrackProperty::TextAnimParam && t.effectIndex >= target) ++t.effectIndex;
    }
    for (Track& t : tracks) l->tracks.add(std::move(t));
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(target);
}

bool Engine::move_text_animator(u64 layerId, u32 from, u32 to) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || !supports_text_animation(*l) || from >= l->text.animators.size() || to >= l->text.animators.size()) return false;
    if (from == to) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "ordenar animadores de texto");
    auto& animators = l->text.animators;
    if (from < to) std::rotate(animators.begin() + from, animators.begin() + from + 1, animators.begin() + to + 1);
    else std::rotate(animators.begin() + to, animators.begin() + from, animators.begin() + from + 1);
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        Track& t = l->tracks.at(i);
        if (t.property != TrackProperty::TextAnimParam) continue;
        if (t.effectIndex == from) t.effectIndex = to;
        else if (from < to && t.effectIndex > from && t.effectIndex <= to) --t.effectIndex;
        else if (from > to && t.effectIndex >= to && t.effectIndex < from) ++t.effectIndex;
    }
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_text_animator(u64 layerId, u32 index, const f32* v) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || !supports_text_animation(*l) || !v || index >= l->text.animators.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "animador de texto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    TextAnimator& a = l->text.animators[index];
    a.enabled = v[0] > 0.5f;
    a.props = static_cast<u32>(std::max(0.0f, v[1])) & 0x7FFu;
    a.selector.basedOn = static_cast<u8>(std::clamp(v[2], 0.0f, 2.0f));
    a.selector.type = static_cast<u8>(std::clamp(v[3], 0.0f, 2.0f));
    a.selector.shape = static_cast<u8>(std::clamp(v[4], 0.0f, 5.0f));
    a.selector.randomOrder = v[5] > 0.5f;
    a.selector.seed = static_cast<u32>(std::clamp(v[6], 0.0f, 1.0e6f));
    a.fill = Vec4{v[28], v[29], v[30], v[31]};
    a.stroke = Vec4{v[32], v[33], v[34], v[35]};
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_text_anim_param(u64 layerId, u32 index, u32 param, f32 value) noexcept {
    if (!std::isfinite(value)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || !supports_text_animation(*l) || index >= l->text.animators.size()) return false;
    f32* r = text_param_ref(l->text.animators[index], param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "valor do animador de texto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    value = clamp_text_param(param, value);
    Track* tr = l->tracks.find(TrackProperty::TextAnimParam, index, param);
    if (tr && !tr->keys.empty()) {
        // Animado: o valor vira keyframe no playhead (como nas outras propriedades).
        const FrameIndex local = l->local_time(playback_.current());
        const u32 k = tr->find_exact(local);
        if (k != kInvalidIndex) tr->keys[k].value = value;
        else (void)tr->set(local, value, Interpolation::Bezier);
    } else {
        *r = value;
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_text_anim_key(u64 layerId, u32 index, u32 param) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || !supports_text_animation(*l) || index >= l->text.animators.size()) return false;
    f32* r = text_param_ref(l->text.animators[index], param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe do animador de texto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const FrameIndex local = l->local_time(playback_.current());
    Track& tr = l->tracks.get_or_create(TrackProperty::TextAnimParam, index, param);
    const u32 k = tr.find_exact(local);
    if (k != kInvalidIndex) {
        // Tirar o último keyframe devolve o valor parado (o do instante).
        if (tr.keys.size() == 1) *r = tr.keys[0].value;
        (void)tr.remove(local);
    } else {
        (void)tr.set(local, tr.keys.empty() ? *r : tr.sample_keys(local), Interpolation::Bezier);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

// =============================================================================
// Expressões
// =============================================================================
namespace {
/// A trilha da chave (property, effectIndex, paramIndex); TimeRemap = o remap.
Track* expression_track(Layer& l, u32 property, u32 effectIndex, u32 paramIndex, bool create) noexcept {
    if (property >= static_cast<u32>(TrackProperty::_Count)) return nullptr;
    const auto p = static_cast<TrackProperty>(property);
    if (p == TrackProperty::TimeRemap) return &l.timeRemap;
    if (p == TrackProperty::EffectParam && !l.find_effect(EffectId{effectIndex, 0})) return nullptr;
    if (p == TrackProperty::TextAnimParam && effectIndex >= l.text.animators.size()) return nullptr;
    if (p == TrackProperty::MaskParam && (paramIndex > 2 || !mask_by_id(l, effectIndex))) return nullptr;
    if (p == TrackProperty::LayerAnimParam && (effectIndex >= l.layerAnimators.size() || paramIndex >= layeranim::kParamCount))
        return nullptr;
    const bool keyed = p == TrackProperty::EffectParam || p == TrackProperty::TextAnimParam || p == TrackProperty::LayerAnimParam || p == TrackProperty::MaskParam;
    const u32 ei = keyed ? effectIndex : kInvalidIndex;
    const u32 pi = keyed ? paramIndex : 0u;
    if (Track* t = l.tracks.find(p, ei, pi)) return t;
    if (!create || l.tracks.size() >= kMaxTrackCount) return nullptr;
    Track& t = l.tracks.get_or_create(p, ei, pi);
    // Trilha nova: o valor parado é o da camada (quem lê por `sample_or`, como
    // o volume, continua vendo o mesmo número).
    t.staticValue = p == TrackProperty::AudioVolume ? 1.0f : expr::static_value(l, t);
    return &t;
}
} // namespace

Status Engine::set_expression(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex, const char* source,
                              expr::Diagnostic* diag) noexcept {
    const u32 key[3] = {property, effectIndex, paramIndex};
    return set_expressions(layerId, key, 1, source, diag);
}

Status Engine::set_expressions(u64 layerId, const u32* keys3, u32 count, const char* source, expr::Diagnostic* diag) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (diag) *diag = expr::Diagnostic{};
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return Status{Errc::NotFound, "camada nao existe"};
    if (!keys3 || count == 0 || count > 16) return Status{Errc::InvalidArgument, "trilhas invalidas"};
    const std::string_view src = source ? std::string_view(source) : std::string_view();
    if (src.size() > expr::kMaxSourceBytes) return Status{Errc::InvalidArgument, "expressao longa demais"};
    const bool remove = src.find_first_not_of(" \t\r\n") == std::string_view::npos;
    // Tudo validado ANTES de mexer: ou todas as trilhas recebem, ou nenhuma.
    bool anyChange = false;
    for (u32 i = 0; i < count; ++i) {
        const u32* k = keys3 + i * 3;
        Track* t = expression_track(*l, k[0], k[1], k[2], false);
        if (!t && !remove) {
            const auto p = k[0] < static_cast<u32>(TrackProperty::_Count) ? static_cast<TrackProperty>(k[0]) : TrackProperty::_Count;
            if (p == TrackProperty::_Count || (p == TrackProperty::EffectParam && !l->find_effect(EffectId{k[1], 0}))
                || (p == TrackProperty::TextAnimParam && k[1] >= l->text.animators.size())
                || (p == TrackProperty::MaskParam && (k[2] > 2 || !mask_by_id(*l, k[1])))
                || (p == TrackProperty::LayerAnimParam && (k[1] >= l->layerAnimators.size() || k[2] >= layeranim::kParamCount))) {
                return Status{Errc::InvalidArgument, "propriedade invalida"};
            }
        }
        anyChange |= remove ? (t && t->expression) : true;
    }
    if (!anyChange) return OkStatus;
    // UM passo de desfazer para o grupo (Posição = X e Y).
    history_.before_mutation(*comp, project_->timeline().current(), remove ? "remover expressão" : "expressão");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const std::shared_ptr<const expr::TrackExpression> compiled = remove ? nullptr : expr::compile(src);
    for (u32 i = 0; i < count; ++i) {
        const u32* k = keys3 + i * 3;
        Track* t = expression_track(*l, k[0], k[1], k[2], !remove);
        if (!t) continue;
        // Cada trilha ganha o SEU objeto (o diagnóstico de execução é por
        // trilha); o programa compilado é o mesmo (cache por texto).
        t->expression = remove ? nullptr : (i == 0 ? compiled : expr::compile(src));
        t->expressionEnabled = true;
    }
    if (diag && compiled) *diag = compiled->parseError;
    project_->mark_dirty();
    request_render();
    return OkStatus;
}

bool Engine::set_expression_enabled(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex, bool enabled) noexcept {
    const u32 key[3] = {property, effectIndex, paramIndex};
    return set_expressions_enabled(layerId, key, 1, enabled);
}

bool Engine::set_expressions_enabled(u64 layerId, const u32* keys3, u32 count, bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !keys3 || count == 0 || count > 16) return false;
    bool found = false, change = false;
    for (u32 i = 0; i < count; ++i) {
        const Track* t = expression_track(*l, keys3[i * 3], keys3[i * 3 + 1], keys3[i * 3 + 2], false);
        if (!t || !t->expression) continue;
        found = true;
        change |= t->expressionEnabled != enabled;
    }
    if (!found) return false;
    if (!change) return true;
    history_.before_mutation(*comp, project_->timeline().current(), enabled ? "ligar expressão" : "desligar expressão");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    for (u32 i = 0; i < count; ++i) {
        Track* t = expression_track(*l, keys3[i * 3], keys3[i * 3 + 1], keys3[i * 3 + 2], false);
        if (t && t->expression) t->expressionEnabled = enabled;
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_expression(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex, ExpressionInfo& out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    out = ExpressionInfo{};
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const Track* t = l ? expression_track(*l, property, effectIndex, paramIndex, false) : nullptr;
    if (!t || !t->expression) return l != nullptr;
    out.exists = true;
    out.enabled = t->expressionEnabled;
    out.source = t->expression->source;
    // Avalia no playhead agora: o erro de execução mostrado é o do instante
    // que a pessoa está vendo, não o de um quadro antigo.
    const expr::Scope scope(project_->timeline());
    const FrameIndex local = l->local_time(playback_.current());
    out.value = t->value_or(local, expr::static_value(*l, *t));
    out.error = t->expression->error();
    return true;
}

u32 Engine::query_expressions(u64 layerId, u32* out, u32 capacityRows) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    u32 n = 0;
    auto put = [&](const Track& t, u32 property) {
        if (!t.expression) return;
        if (out && n < capacityRows) {
            out[n * 4 + 0] = property;
            out[n * 4 + 1] = t.effectIndex;
            out[n * 4 + 2] = t.effectParamIndex;
            out[n * 4 + 3] = (t.expressionEnabled ? 1u : 0u) | (t.expression->error().ok ? 0u : 2u);
        }
        ++n;
    };
    put(l->timeRemap, static_cast<u32>(TrackProperty::TimeRemap));
    for (u32 i = 0; i < l->tracks.size(); ++i) put(l->tracks.at(i), static_cast<u32>(l->tracks.at(i).property));
    return std::min(n, capacityRows);
}

bool Engine::apply_text_preset(u64 layerId, u32 preset) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || !supports_text_animation(*l) || preset >= text::kTextPresetCount) return false;
    Layer prepared = *l;
    if (prepared.kind == LayerKind::Model3D && !ensure_text3d_layout(prepared).ok()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "preset de texto");
    *l = std::move(prepared);
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const f64 fps = comp->fps();
    const i64 at = l->contains_time(playback_.current()) ? playback_.current().value : l->start.value;
    const i64 begin = std::min(at, std::max(l->start.value, l->end.value - 3));
    const i64 s = l->local_time(FrameIndex{begin}).value;
    const i64 len = std::max<i64>(2, l->end.value - begin);
    // A entrada leva ~1 s (no máximo metade da camada).
    const i64 d = std::clamp<i64>(static_cast<i64>(std::lround(fps)), 2, std::max<i64>(2, len / 2));
    const bool ok = text::apply_text_preset(preset, l->text, l->tracks, s, d, fps);
    if (ok && preset == 11) {
        // The source includes Motion Tile and pixel motion blur. Preserve the
        // tiling values; native shutter sampling is used for animated glyphs.
        presets::Preset tile;
        const char* data = R"({"aurea_preset":1,"kind":"effects","name":"Juan Motion Tile","fps":60,"effects":[{"key":"aurea.stylize.motion_tile","params":[{"id":"output_width","v":[300,0,0,0]},{"id":"output_height","v":[300,0,0,0]}]}]})";
        if (presets::parse(data, tile, &effectRegistry_)) (void)presets::apply(tile, *l, s, 0, fps, &effectRegistry_);
        l->motionBlur = true;
        comp->motion_blur().enabled = true;
        comp->motion_blur().shutterAngle = 180;
        comp->motion_blur().samples = std::max(32u, comp->motion_blur().samples);
    }
    if (ok && begin > l->start.value) {
        // Applying at the playhead must not retroactively hide or transform
        // the text before the requested animation starts.
        for (u32 i = 0; i < l->text.animators.size(); ++i) {
            Track& amount = l->tracks.get_or_create(TrackProperty::TextAnimParam, i, text::kSelAmount);
            if (amount.expression) continue; // Juan expressions gate localTime themselves.
            (void)amount.set(l->local_time(l->start), 0.0f, Interpolation::Hold);
            (void)amount.set(FrameIndex{s}, 100.0f, Interpolation::Hold);
        }
    }
    project_->mark_dirty();
    request_render();
    return ok;
}

std::string Engine::layer_media_path(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || (l->kind != LayerKind::Video && l->kind != LayerKind::Audio)) return {};
    const Asset* a = project_->asset(l->source);
    return a ? resolve_asset_path(a->sourcePath) : std::string{};
}

namespace {
/// Tira as legendas de `src` (índices de trás para a frente).
u32 drop_captions(Composition& comp, u64 src) {
    std::vector<LayerId> ids;
    u32 removed = 0;
    for (u32 i = 0; i < comp.order().size(); ++i) {
        const LayerId id = comp.order().at(i);
        const Layer* l = comp.layer(id);
        if (l && l->kind == LayerKind::Text && l->text.captionSource == src) {
            // Uma faixa guarda vários blocos: conta legendas, não camadas, para
            // o número bater com `caption_count` (o que a UI mostra).
            removed += l->captions.empty() ? 1u : static_cast<u32>(l->captions.size());
            ids.push_back(id);
        }
    }
    for (const LayerId id : ids) comp.remove_layer(id);
    return removed;
}
} // namespace

Result<std::vector<text::CaptionWord>> Engine::transcribe_local(u64 layerId, const std::string& model, const std::string& language) noexcept {
    const auto path = layer_media_path(layerId);
    if (path.empty() || !config_.mediaFactory) return Status{Errc::InvalidArgument, "camada sem audio"};
    captionCancelled.store(false); captionProgress.store(0);
    return text::transcribe_local(*config_.mediaFactory, path, model, language, captionCancelled,
        [this](int value) { captionProgress.store(value); });
}

Result<u32> Engine::create_captions(u64 sourceLayer, const std::vector<text::CaptionWord>& words,
                                    const text::CaptionOptions& opt) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    const Layer* src = comp->layer(LayerId::unpack(sourceLayer));
    if (!src || (src->kind != LayerKind::Video && src->kind != LayerKind::Audio)) return Status{Errc::NotFound, "camada sem fala"};
    if (!text::default_font()) return Status{Errc::NotSupported, "nenhuma fonte disponivel neste aparelho"};
    const std::vector<text::CaptionGroup> groups = text::group_captions(words, opt);
    if (groups.empty()) return Status{Errc::InvalidArgument, "nenhuma palavra"};
    history_.before_mutation(*comp, project_->timeline().current(), "gerar legendas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const f64 fps = comp->fps();
    // Segundo da mídia → quadro da timeline: a MESMA conta de tempo da fonte
    // (velocidade, reverso e remapeamento inclusos), procurada quadro a quadro.
    const i64 s0 = src->start.value, s1 = std::max(src->start.value + 1, src->end.value);
    std::vector<f64> srcSec(static_cast<usize>(s1 - s0));
    for (i64 f = s0; f < s1; ++f) srcSec[static_cast<usize>(f - s0)] = src->source_frame(FrameIndex{f}) / fps;
    const f64 half = 0.5 / fps;
    // Fase 8D: a busca era linear na camada inteira POR palavra (5000 palavras
    // num vídeo de 30 min = ~10^8 comparações, 104 ms no host). Com o tempo da
    // fonte crescente (o caso normal: sem reverso nem curva que volta) é uma
    // busca binária; senão, a varredura de antes (mesmo resultado).
    const bool monotonic = std::is_sorted(srcSec.begin(), srcSec.end());
    auto frame_of = [&](f64 sec) -> i64 {
        if (monotonic) {
            const auto it = std::lower_bound(srcSec.begin(), srcSec.end(), sec,
                                             [half](f64 v, f64 target) { return v + half < target; });
            return it == srcSec.end() ? -1 : s0 + static_cast<i64>(it - srcSec.begin());
        }
        for (usize i = 0; i < srcSec.size(); ++i) if (srcSec[i] + half >= sec) return s0 + static_cast<i64>(i);
        return -1;   // depois do fim da camada
    };
    const u64 key = sourceLayer;
    drop_captions(*comp, key);
    const u32 shortSide = std::min(comp->width(), comp->height());
    const f32 posY = std::clamp(opt.posY, 0.1f, 0.9f);
    const LayerId captionId = comp->add_layer(LayerKind::Text, "Legendas");
    Layer* track = comp->layer(captionId);
    if (!track) return Status{Errc::OutOfMemory, "faixa de legendas"};
    track->text.captionSource = key;
    track->start = FrameIndex{0}; track->end = comp->duration();
    track->captionOptions = opt;
    text::apply_caption_style(opt, shortSide, track->text, track->tracks, {}, 0);
    track->transform.position = Vec3{static_cast<f32>(comp->width()) * .5f, static_cast<f32>(comp->height()) * posY, 0};
    u32 made = 0;
    for (usize g = 0; g < groups.size(); ++g) {
        const text::CaptionGroup& cg = groups[g];
        const i64 a = frame_of(cg.start);
        if (a < 0) break;
        // Até a próxima legenda (sem buraco pequeno) ou o fim da fala + 8 quadros.
        i64 b = frame_of(cg.end);
        if (b < 0) b = s1;
        b = std::max(b + std::min<i64>(8, static_cast<i64>(std::lround(fps * 0.25))), a + 1);
        if (g + 1 < groups.size()) {
            const i64 n = frame_of(groups[g + 1].start);
            if (n >= 0 && (n < b || n - b < static_cast<i64>(std::lround(fps * 0.3)))) b = std::max(n, a + 1);
        }
        b = std::min(b, s1);
        if (b <= a) continue;
        text::CaptionSegment segment;
        segment.id = ++made; segment.start = a; segment.end = b; segment.text = cg.text;
        for (u32 w = 0; w < cg.count; ++w) {
            const auto& input = words[cg.first + w];
            i64 begin = frame_of(input.start), finish = frame_of(input.end);
            begin = std::clamp(begin < 0 ? b - 1 : begin, a, b - 1);
            finish = std::clamp(finish < 0 ? b : finish, begin + 1, b);
            segment.words.push_back({input.text, begin, finish});
        }
        track->captions.push_back(std::move(segment));
    }
    if (track->captions.empty()) { comp->remove_layer(captionId); }
    else { track->text.content = track->captions.front().text; recenter_text(*track); }
    project_->mark_dirty();
    request_render();
    return made;
}

u32 Engine::remove_captions(u64 sourceLayer) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || caption_count_locked(*comp, sourceLayer) == 0) return 0;
    history_.before_mutation(*comp, project_->timeline().current(), "remover legendas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const u32 n = drop_captions(*comp, sourceLayer);
    project_->mark_dirty();
    request_render();
    return n;
}

u32 Engine::caption_count_locked(const Composition& comp, u64 sourceLayer) const noexcept {
    u32 n = 0;
    for (u32 i = 0; i < comp.order().size(); ++i) {
        const Layer* l = comp.layer(comp.order().at(i));
        if (l && l->kind == LayerKind::Text && l->text.captionSource == sourceLayer) n += l->captions.empty() ? 1 : static_cast<u32>(l->captions.size());
    }
    return n;
}

u32 Engine::caption_count(u64 sourceLayer) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    return comp ? caption_count_locked(*comp, sourceLayer) : 0;
}

std::string Engine::text_font(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text) return {};
    return l->text.fontFamily + "\t" + std::to_string(l->text.fontWeight) + "\t" + (l->text.fontItalic ? "1" : "0") + "\t"
         + (l->text.fontPath.empty() ? std::string{} : resolve_asset_path(l->text.fontPath));
}

bool Engine::is_shape3d_layer(const Layer& layer) const noexcept {
    const Asset* a = layer.kind == LayerKind::Model3D && project_ ? project_->asset(layer.model.scene) : nullptr;
    return a && scene3d::is_shape3d_source(a->sourcePath);
}

bool Engine::supports_text_animation(const Layer& layer) const noexcept {
    if (layer.kind == LayerKind::Text) return true;
    if (layer.kind != LayerKind::Model3D || !project_) return false;
    const Asset* asset = project_->asset(layer.model.scene);
    return asset && asset->sourcePath.rfind(scene3d::kText3DScheme, 0) == 0;
}

Status Engine::ensure_text3d_layout(Layer& layer) noexcept {
    if (layer.locked) return Status{Errc::InvalidState, "camada bloqueada"};
    const Asset* old = project_->asset(layer.model.scene);
    scene3d::Text3DSpec spec;
    if (layer.kind != LayerKind::Model3D || !old || !scene3d::decode_text3d(old->sourcePath, spec))
        return Status{Errc::InvalidArgument, "selecione texto 3D"};
    // TextData owns shared presets and their tracks; the recipe owns geometry.
    layer.text.content = spec.content;
    layer.text.size = 100.f;
    layer.text.alignment = spec.alignment;
    if (spec.separateGlyphs) return OkStatus;
    spec.separateGlyphs = true;
    const auto font = scene3d::text3d_font(spec);
    if (!font) return Errc::NotFound;
    auto result = scene3d::build_text3d(*font, spec);
    if (!result.ok()) return Status{Errc::InvalidArgument, "nao foi possivel separar as letras (limite de 256 glifos)"};
    std::shared_ptr<const scene3d::SceneAsset> scene(std::move(result.asset));
    const auto id = add_model_asset(*project_, *scene, "Texto 3D", scene3d::encode_text3d(spec));
    models_[id.pack()] = scene;
    layer.model.scene = id;
    return OkStatus;
}

Result<u64> Engine::add_text3d(const scene3d::Text3DSpec& spec) noexcept {
    const auto font = scene3d::text3d_font(spec);
    if (!font) return Status{Errc::NotSupported, "nenhuma fonte disponivel neste aparelho"};
    scene3d::ImportResult r = scene3d::build_text3d(*font, spec);
    if (!r.ok()) return Status{Errc::InvalidArgument, "texto sem letras visiveis"};
    std::shared_ptr<const scene3d::SceneAsset> scene(std::move(r.asset));
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    history_.before_mutation(*comp, project_->timeline().current(), "texto 3D");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const AssetId assetId = add_model_asset(*project_, *scene, "Texto 3D", scene3d::encode_text3d(spec));
    models_[assetId.pack()] = scene;
    const LayerId lid = comp->add_layer(LayerKind::Model3D, "Texto 3D");
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->threeD = true;
    l->model.scene = assetId;
    l->model.animationClip = -1; // Text 3D moves only through user-authored layer transforms.
    l->text.content = spec.content;
    l->text.size = 100.f;
    l->text.alignment = spec.alignment;
    // Letra com ~25 % da altura da composição; texto longo encolhe para caber
    // em 80 % da largura.
    const Vec3 ext = scene->bounds.extent();
    const f32 w = static_cast<f32>(comp->width()), h = static_cast<f32>(comp->height());
    l->model.unitScale = std::min(0.25f * h, 0.8f * w / std::max(ext.x, 1e-3f));
    l->model.pivot = scene->bounds.center();
    l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    l->transform.scale = Vec3{1.0f, 1.0f, 1.0f};
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

Status Engine::set_text3d(u64 layerId, const scene3d::Text3DSpec& spec) noexcept {
    scene3d::Text3DSpec edited = spec;
    AssetId previousScene;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->kind != LayerKind::Model3D || l->locked) return Errc::InvalidArgument;
        previousScene = l->model.scene;
        // A stale panel snapshot must not merge the letters of a live effect.
        if (std::any_of(l->effects.begin(), l->effects.end(), [](const EffectInstance& effect) {
            return effect.type == effect_type_id(text::kAnimatorEffect) || effect.type == effect_type_id(text::kTransformEffect) || effect.type == effect_type_id(effect_keys::kText3DLayout);
        }) || !l->text.animators.empty() || layeranim::has_units(*l)) edited.separateGlyphs = true;
    }
    const auto font = scene3d::text3d_font(edited);
    if (!font) return Status{Errc::NotSupported, "nenhuma fonte disponivel neste aparelho"};
    scene3d::ImportResult r = scene3d::build_text3d(*font, edited);
    if (!r.ok()) return Status{Errc::InvalidArgument, "texto sem letras visiveis"};
    std::shared_ptr<const scene3d::SceneAsset> scene(std::move(r.asset));
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D || l->locked || l->model.scene != previousScene) return Errc::InvalidArgument;
    const Asset* old = project_->asset(l->model.scene);
    scene3d::Text3DSpec prev;
    if (!old || !scene3d::decode_text3d(old->sourcePath, prev)) return Errc::InvalidArgument;
    history_.before_mutation(*comp, project_->timeline().current(), "editar texto 3D");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    // Asset novo (o antigo fica para o desfazer religar a malha anterior).
    const AssetId assetId = add_model_asset(*project_, *scene, "Texto 3D", scene3d::encode_text3d(edited));
    models_[assetId.pack()] = scene;
    l->model.scene = assetId;
    l->model.animationClip = -1; // Text 3D moves only through user-authored layer transforms.
    l->model.pivot = scene->bounds.center();
    l->text.content = edited.content;
    l->text.size = 100.f;
    l->text.alignment = edited.alignment;
    project_->mark_dirty();
    request_render();
    return OkStatus;
}

bool Engine::query_text3d(u64 layerId, scene3d::Text3DSpec& out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D) return false;
    const Asset* a = project_->asset(l->model.scene);
    return a && scene3d::decode_text3d(a->sourcePath, out);
}

std::shared_ptr<const scene3d::SceneAsset> Engine::model_asset(u64 assetId) const noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const auto it = models_.find(assetId);
    return it == models_.end() ? nullptr : it->second;
}

std::vector<std::string> Engine::model_missing_textures(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D) return {};
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second) return {};
    return it->second->missingTextures;
}

std::string Engine::model_folder(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D) return {};
    const Asset* a = project_->asset(l->model.scene);
    scene3d::Text3DSpec spec;
    if (!a || a->sourcePath.empty() || scene3d::decode_text3d(a->sourcePath, spec) || scene3d::is_shape3d_source(a->sourcePath)) return {};
    const std::string path = resolve_asset_path(a->sourcePath);
    const usize slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

Result<u32> Engine::reload_model_textures(u64 layerId, std::string* detail) noexcept {
    AssetId old{};
    Asset copy;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        const Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->kind != LayerKind::Model3D) return Status{Errc::InvalidArgument, "selecione um modelo 3D"};
        const Asset* a = project_->asset(l->model.scene);
        scene3d::Text3DSpec spec;
        if (!a || a->sourcePath.empty() || scene3d::decode_text3d(a->sourcePath, spec) || scene3d::is_shape3d_source(a->sourcePath))
            return Status{Errc::InvalidArgument, "selecione um modelo 3D importado"};
        old = l->model.scene;
        copy = *a;
    }
    // Parse e texturas FORA do lock, como no import.
    scene3d::ImportOptions options;
    options.maxTextureSize = model_texture_cap();
    scene3d::ImportResult r = scene3d::import_scene_file(resolve_asset_path(copy.sourcePath), options);
    if (!r.ok()) {
        if (detail) *detail = r.detail;
        return Status{r.error == scene3d::ImportError::FileNotFound ? Errc::NotFound : Errc::AssetCorrupted,
                      scene3d::to_string(r.error)};
    }
    std::shared_ptr<const scene3d::SceneAsset> scene(std::move(r.asset));
    if (detail) {
        detail->clear();
        for (const std::string& w : scene->warnings) *detail += w + "\n";
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "projeto fechado durante o import"};
    history_.before_mutation(*comp, project_->timeline().current(), "importar texturas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    // Asset novo com o mesmo caminho guardado: a GPU guarda o modelo pelo id
    // do asset, então o id novo faz as texturas subirem sem mexer no render.
    const AssetId fresh = project_->add_asset(std::move(copy));
    models_[fresh.pack()] = scene;
    project_->timeline().for_each_composition([&](CompositionId, Composition& c) {
        for (u32 i = 0; i < c.order().size(); ++i) {
            Layer* l = c.layer(c.order().at(i));
            if (l && l->kind == LayerKind::Model3D && l->model.scene == old) l->model.scene = fresh;
        }
    });
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(scene->missingTextures.size());
}

std::shared_ptr<const scene3d::SceneAsset> Engine::model_lookup(void* self, AssetId id) {
    // Chamado pelo renderer DENTRO do prepare (modelo já travado).
    Engine* e = static_cast<Engine*>(self);
    const auto it = e->models_.find(id.pack());
    return it == e->models_.end() ? nullptr : it->second;
}

const ImagePixels* Engine::image_lookup(void* self, AssetId id) {
    Engine* e = static_cast<Engine*>(self);
    const auto it = e->images_.find(id.pack());
    return it == e->images_.end() ? nullptr : &it->second;
}

// =============================================================================
// A fronteira
// =============================================================================
u32 Engine::submit_commands(const Command* commands, u32 count,
                            const char* stringBlob, u32 stringBlobSize) noexcept {
    if (!commands || count == 0) return 0;
    // Serialize publication with project replacement / queue reset, including
    // the string arena, without blocking the UI on rendering's model lock.
    std::lock_guard<std::mutex> lock(commandSubmitMutex_);
    const EngineState s = state_;
    if (s != EngineState::Ready && s != EngineState::Rendering && s != EngineState::Suspended) return 0;

    // As strings do lote vêm com deslocamento RELATIVO ao blob do lote: o
    // blob é copiado para a arena da fila e cada comando com string é
    // rebaseado para onde a cópia caiu (sem isso, o comando apontava para a
    // string de um lote anterior — um texto editado virava "editar texto").
    u32 base = 0;
    bool haveBlob = false;
    if (stringBlob && stringBlobSize) {
        if (char* dst = commandQueue_->alloc_string(stringBlobSize)) {
            std::memcpy(dst, stringBlob, stringBlobSize);
            base = commandQueue_->offset_of(dst);
            haveBlob = true;
        }
    }
    u32 accepted = 0;
    for (u32 i = 0; i < count; ++i) {
        Command c = commands[i];
        // Sem blob no lote, o deslocamento já é da arena (push_string direto).
        if (c.stringLength > 0 && stringBlob && stringBlobSize) {
            // Sem estouro de u32: offset e comprimento vêm da UI (e do fuzz).
            if (!haveBlob || c.stringOffset > stringBlobSize || c.stringLength > stringBlobSize - c.stringOffset) {
                // A lost text payload must never become an edit to empty text.
                // Reject this command and leave the previous content intact.
                continue;
            } else {
                c.stringOffset += base;
            }
        }
        if (commandQueue_->push(c) == kInvalidIndex) break;
        ++accepted;
    }
    commandQueue_->commit();
    request_render();
    return accepted;
}

void Engine::drain_commands_locked() noexcept {
    if (!project_) return;
    commandQueue_->drain([this](const Command& cmd) {
        // O blob da fila guarda as strings coladas, SEM terminador: cópia com o
        // comprimento exato (lida até o NUL, um texto novo levava junto os
        // anteriores — "Texto ATexto AuTexto…").
        std::string owned;
        const char* str = cmd.type == CommandType::TextSetContent && cmd.stringLength == 0 ? "" : nullptr;
        if (cmd.stringLength > 0) {
            if (const char* raw = commandQueue_->string_at(cmd.stringOffset, cmd.stringLength)) {
                owned.assign(raw, cmd.stringLength);
                str = owned.c_str();
            } else {
                AUREA_LOG_WARN("comando %u recusado: string fora do lote", static_cast<unsigned>(cmd.type));
                return;
            }
        }
        const Status s = apply_command_internal(cmd, str, true);
        if (!s.ok()) {
            AUREA_LOG_WARN("comando %u recusado: %s", static_cast<unsigned>(cmd.type), s.message().data());
        } else if (mutates_model(cmd.type) || cmd.type == CommandType::CompositionCreate || cmd.type == CommandType::CompositionDelete) {
            project_->mark_dirty();
        }
    });
}

RenderSettings Engine::current_render_settings() noexcept {
    RenderSettings rs;
    rs.sceneEditor = sceneEditor_;
    rs.mediaGeneration = playback_.media_epoch();
    if (rawPlaybackLayer_.valid()) {
        rs.rawPlayback = true;
        rs.gpuTimers = true;
        return rs; // Original resolution, neutral viewport; no adaptive downscaling.
    }
    // AUTO 2.0: os botões de redução decididos pela medição, nunca acima do
    // piso térmico do instante (o calor muda entre dois quadros medidos).
    rs.quality = adapt().quality().min(PreviewQuality::level(thermal_heavy_level()));
    rs.heavyScale = preview_heavy_scale();
    rs.previewNumerator = adapt().current_numerator();
    rs.previewDenominator = adapt().current_denominator();
    if (refineNow_) {
        const u32 heavy = std::max(adapt().still_heavy_level(caps_.thermal()), thermal_heavy_level());
        rs.quality = PreviewQuality::level(heavy);
        rs.heavyScale = caps_.policy().heavyScale;
        rs.previewNumerator = 1;
        rs.previewDenominator = adapt().still_denominator(caps_.thermal());
    }
    rs.gpuTimers = config_.enableTelemetry;
    if (project_) {
        rs.viewportZoom = project_->editor_settings().viewportZoom > 0.0f
                        ? project_->editor_settings().viewportZoom : 1.0f;
        rs.viewportPan = project_->editor_settings().viewportPan;
    }
    return rs;
}

Status Engine::recover_device_locked() noexcept {
    // Dispositivo perdido (driver reiniciou, GPU resetou): tudo que o driver
    // tinha morreu. O PROJETO não — ele mora na CPU. Recria o backend, o
    // renderer e a superfície, e o próximo frame sai normal.
    AUREA_LOG_WARN("dispositivo de GPU perdido: recriando");
    media_.close_all();   // os frames importados pertenciam ao dispositivo morto
    renderer_.forget_device();
    gpu_->shutdown();
    if (const Status s = gpu_->initialize(config_.backendConfig); !s.ok()) return s;
    renderer_.set_model_lookup(&Engine::model_lookup, this);
    if (const Status s = renderer_.initialize(*gpu_, effectRegistry_); !s.ok()) return s;
    if (surface_.nativeWindow) {
        const Status s = gpu_->attach_surface(surface_);
        surfaceAttached_ = s.ok();
        if (!s.ok()) return s;
    }
    return OkStatus;
}

Status Engine::render_frame(bool onlyIfChanged) noexcept {
    // Export em andamento: o último quadro do preview fica na tela; a GPU e
    // os decoders são do export.
    if (exportActive_.load(std::memory_order_acquire)) return OkStatus;
    const u64 frameStart = monotonic_ns();
    std::lock_guard<std::mutex> rl(renderMutex_);
    lastSkipped_ = false;

    if (const u64 size = pendingSurfaceSize_.exchange(0, std::memory_order_acq_rel)) {
        const u32 width = static_cast<u32>(size >> 32), height = static_cast<u32>(size);
        if (width != surface_.width || height != surface_.height) {
            if (gpu_ && surfaceAttached_) {
                if (const Status s = gpu_->resize_surface(width, height); !s.ok()) {
                    set_last_error(s, "redimensionar preview");
                    u64 empty = 0;
                    pendingSurfaceSize_.compare_exchange_strong(empty, size, std::memory_order_acq_rel);
                    return s;
                }
            }
            surface_.width = width;
            surface_.height = height;
        }
        forceRender_.store(true, std::memory_order_release);
    }

    RenderSettings rs;
    FrameIndex t{0};
    bool playing = false;
    f32 renderSpeed = 1.0f;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Errc::InvalidState;
        drain_commands_locked();
        Composition* comp = current_composition();
        if (!comp) return Errc::NotFound;

        const Layer* rawLayer = comp->layer(rawPlaybackLayer_);
        const Asset* rawAsset = rawLayer ? project_->asset(rawLayer->source) : nullptr;
        // Keep timeline controls in project-frame units. RAW scheduling below
        // uses source PTS directly, independently of the composition frame grid.
        const FrameIndex rawDuration{rawAsset ? static_cast<i64>(std::ceil(rawAsset->video.frameCount.value * comp->fps() / std::max(1.0, rawAsset->video.fps))) : 0};
        playback_.configure(comp->fps(), rawAsset ? rawDuration : comp->duration());
        // Antes do update: um seek/play desta leva recomeça o som no ponto
        // novo (senão o relógio do áudio ainda diria o instante antigo).
        sync_audio_locked(*comp);
        t = playback_.update(frameStart);
        // Depois: o loop acontece dentro do update.
        sync_audio_locked(*comp);
        project_->timeline().set_playhead(t);
        playing = playback_.playing();
        renderSpeed = playback_.speed();
        if (playingHint_.exchange(playing) != playing) media_.proxies().set_pause_reason(PreviewProxyService::Playback, playing);

        if (!gpu_ || !renderer_.ready()) return OkStatus;   // sem GPU: só o modelo avança
        if (gpu_->is_device_lost()) {
            if (const Status s = recover_device_locked(); !s.ok()) return s;
        }

        // Nada mudou desde o último frame apresentado? Então não há o que
        // redesenhar: mesmo frame do playhead, nenhum comando/superfície nova,
        // e nenhum frame de vídeo que estava faltando chegou.
        const u64 mediaGen = mediaReadyGen_.load(std::memory_order_acquire);
        // Toda mudança do modelo redesenha — inclusive as que não vêm pela fila
        // de comandos (importar imagem/vídeo/modelo, extrair áudio, congelar).
        const u32 rev = modelRevision_.load(std::memory_order_acquire);
        const bool force = forceRender_.exchange(false, std::memory_order_acq_rel) || rev != lastRenderedRevision_;
        lastRenderedRevision_ = rev;
        if (onlyIfChanged && !(rawAsset && playing) && !force && t.value == lastRenderedFrame_
            && !(lastIncomplete_ && mediaGen != lastMediaGen_)) {
            lastSkipped_ = true;
            if (playing) {
                const f32 speed = std::max(0.05f, std::fabs(playback_.speed()));
                const f64 fps = playback_.fps() > 0.0 ? playback_.fps() : 30.0;
                const i64 next = t.value + (playback_.direction() < 0 ? -1 : 1);
                const i64 nextNs = static_cast<i64>(std::llround(static_cast<f64>(next) * 1e9 / fps));
                const i64 deltaNs = std::llabs(nextNs - playback_.current_ns());
                nextFrameDueNs_ = frameStart + static_cast<u64>(static_cast<f64>(deltaNs) / speed);
            }
            return OkStatus;
        }
        lastMediaGen_ = mediaGen;

        rs = current_render_settings();
        const DecodeMode mode = playing ? DecodeMode::Playback
                              : playback_.mode() == PlaybackMode::Scrubbing ? DecodeMode::Scrub
                                                                            : DecodeMode::Still;
        if (rs.rawPlayback && rawLayer && rawAsset) {
            renderer_.prepare_raw(rawPlaybackLayer_, *rawLayer, *rawAsset, playback_.current_ns() / 1000, media_, ++frameCounter_,
                                  mode, playback_.direction(), playback_.speed(), rs.mediaGeneration, snapshot_);
        } else {
            renderer_.prepare(*comp, *project_, t, &media_, &Engine::image_lookup, this, rs, ++frameCounter_,
                              playback_.direction(), mode, playback_.speed(), snapshot_);
        }
    }
    const u64 tPrepared = monotonic_ns();

    // Sem superfície o quadro não é apresentado: a mudança continua pendente
    // para o primeiro quadro com superfície (senão a tela nova fica vazia).
    if (!surfaceAttached_) {
        forceRender_.store(true, std::memory_order_release);
        return OkStatus;
    }

    FrameStats stats;
    RenderTimings timings;
    timings.cpuPrepareMs = static_cast<f32>(static_cast<f64>(tPrepared - frameStart) * 1e-6);
    // Também TOCANDO: uma camada de vídeo sem quadro nenhum (decoder abrindo
    // no corte, primeiro quadro a caminho) segura a última imagem inteira em
    // vez de apresentar a composição sem ela — preto no meio do play. A
    // espera é limitada (PreviewRefill); o relógio segue, nada atrasa o som.
    const bool refill = previewRefill_.hold(snapshot_.missingVideoFrames > 0,
        !rs.rawPlayback && lastRenderedFrame_ >= 0, frameStart);
    if (refill) {
        snapshot_.release_video_frames();
        nextFrameDueNs_ = frameStart + static_cast<u64>(1e9 / std::max(1.0f, config_.displayRefreshRate));
        lastIncomplete_ = true; lastSkipped_ = true;
        // Recheck even when decode fails silently, so the bounded hold expires.
        forceRender_.store(true, std::memory_order_release);
        return OkStatus;
    }
    // Keep the last presented image during refill, rather than presenting a
    // fabricated black frame after seek. Decode continues asynchronously.
    if (rs.rawPlayback && snapshot_.missingVideoFrames) {
        nextFrameDueNs_ = frameStart + static_cast<u64>(1e9 / std::max(1.0f, config_.displayRefreshRate));
        lastIncomplete_ = true;
        lastRenderedFrame_ = t.value;
        lastSkipped_ = true;
        return OkStatus;
    }
    if (rs.rawPlayback && playing) {
        std::lock_guard<std::mutex> perfLock(perfMutex_);
        if (playbackMeasurement_.epoch == rs.mediaGeneration && playbackMeasurement_.lastPts == snapshot_.playbackPtsUs) {
            // A video-only renderer has nothing new to draw. Keep the previous
            // image while the decoder catches up; never burn another GPU pass.
            ++playbackMeasurement_.repeated;
            const i64 interval = snapshot_.playbackDurationUs > 0 ? snapshot_.playbackDurationUs
                : static_cast<i64>(1e6 / std::max(1.0, snapshot_.playbackStream.fps));
            const i64 remainingUs = snapshot_.playbackPtsUs + interval - snapshot_.playbackTargetUs;
            nextFrameDueNs_ = frameStart + (remainingUs > 0
                ? static_cast<u64>(remainingUs * 1000 / std::max(0.05f, renderSpeed))
                : static_cast<u64>(1e9 / std::max(1.0f, config_.displayRefreshRate)));
            snapshot_.release_video_frames();
            lastRenderedFrame_ = t.value;
            lastIncomplete_ = true;
            lastSkipped_ = true;
            return OkStatus;
        }
    }
    const Status s = renderer_.render(snapshot_, rs, nullptr, stats, timings);
    if (snapshot_.playbackPtsUs >= 0) {
        const auto& stream = snapshot_.playbackStream;
        const auto& decode = snapshot_.playbackDecode;
        const auto audio = audio_.stats();
        std::lock_guard<std::mutex> perfLock(perfMutex_);
        auto& m = playbackMeasurement_;
        const bool contiguous = m.playing && playing && m.epoch == rs.mediaGeneration;
        if (contiguous && m.lastNs) {
            m.activeNs += frameStart - m.lastNs;
            if (decode.framesDelivered >= m.lastDecoded) m.decoded += decode.framesDelivered - m.lastDecoded;
        }
        m.lastDecoded = decode.framesDelivered;
        if (s.ok() && playing) {
            if (contiguous && m.lastPts == snapshot_.playbackPtsUs) ++m.repeated;
            else {
                ++m.presented;
                if (contiguous) ++m.rateFrames;
                const i64 frameUs = m.lastDuration > 0 ? m.lastDuration : static_cast<i64>(1e6 / std::max(1.0, stream.fps));
                if (contiguous && snapshot_.playbackPtsUs > m.lastPts + frameUs)
                    m.dropped += static_cast<u64>(std::max<i64>(0, (snapshot_.playbackPtsUs - m.lastPts + frameUs/2) / frameUs - 1));
            }
            m.lastPts = snapshot_.playbackPtsUs;
            m.lastDuration = snapshot_.playbackDurationUs;
        }
        m.epoch = rs.mediaGeneration; m.lastNs = frameStart; m.playing = playing;
        const double seconds = m.activeNs * 1e-9;
        char report[1536];
        std::snprintf(report, sizeof(report),
            "%s\n%ux%u %s | pixel=%u\n%s | %s\nexpected=%.2f presented=%.2f fps\nunique=%llu repeated=%llu dropped_est=%llu\ndecoded=%llu decoded_fps=%.2f seeks=%llu queue=%u (%llu bytes)\naudio=%u ms underruns=%llu missing=%llu\ndecode=%.2f ms prepare=%.2f render=%.2f present=%.2f\nav_late=%.2f ms pts=%lld target=%lld\nactive=%.2f s passes=%u upload_cpu=%.3f ms gpu=%.2f ms gpu_measured=%d",
            rs.rawPlayback ? "AUREA RAW PLAYBACK TEST" : "AUREA COMPOSITOR PLAYBACK TEST",
            stream.display_width(), stream.display_height(), stream.codec, snapshot_.playbackPixelFormat,
            stream.decoderName, stream.hardwareDecoder ? "hardware" : "software/unknown",
            stream.fps, seconds > 0 ? m.rateFrames / seconds : 0,
            static_cast<unsigned long long>(m.presented), static_cast<unsigned long long>(m.repeated), static_cast<unsigned long long>(m.dropped),
            static_cast<unsigned long long>(decode.framesDelivered), seconds > 0 ? m.decoded / seconds : 0, static_cast<unsigned long long>(decode.seeks), decode.cache.frames, static_cast<unsigned long long>(decode.cache.bytes),
            audio.queuedMs, static_cast<unsigned long long>(audio.underruns), static_cast<unsigned long long>(audio.missingBlocks),
            decode.decodeMsAvg, timings.cpuPrepareMs, timings.cpuRecordMs, timings.presentMs,
            (snapshot_.playbackTargetUs-snapshot_.playbackPtsUs)*0.001,
            static_cast<long long>(snapshot_.playbackPtsUs), static_cast<long long>(snapshot_.playbackTargetUs), seconds, stats.passesExecuted, timings.videoUploadMs, timings.gpuTotalMs, timings.gpuMeasured ? 1 : 0);
        m.report = report;
    }
    if (!s.ok() && s.code() != Errc::SurfaceLost && s.code() != Errc::Timeout) {
        AUREA_LOG_WARN("frame nao renderizado: %s", s.message().data());
    }
    if (s.code() == Errc::SurfaceLost || s.code() == Errc::Timeout) request_render();

    lastRenderedFrame_ = t.value;
    lastIncomplete_ = snapshot_.missingVideoFrames > 0 || snapshot_.staleVideoFrames > 0;
    lastCulledLayers_.store(snapshot_.culledLayers, std::memory_order_relaxed);
    // Recurso pendente: tenta de novo nos próximos quadros — no máximo ~1 s
    // (uma camada que nunca fica pronta não pode prender a GPU em laço).
    if (renderer_.take_incomplete()) {
        if (++incompleteRetries_ <= 60) {
            forceRender_.store(true, std::memory_order_release);
            request_render();
        }
    } else {
        incompleteRetries_ = 0;
    }
    if (!s.ok()) forceRender_.store(true, std::memory_order_release);
    if (s.ok()) frameScheduler_.presented(t, playing);
    stats.frameIndex = static_cast<u32>(frameCounter_);
    stats.cpuMs = timings.cpuPrepareMs + timings.cpuRecordMs;
    stats.decodeMs = media_.stats().decodeMsAvg;
    stats.droppedFrames = frameScheduler_.dropped_total();
    stats.cpuMemoryBytes = memory_.total_used();
    stats.memoryPressure = memory_.pressure();
    // O refino não entra na média do AUTO: é um quadro avulso em outra
    // resolução, não o ritmo do preview.
    if (!refineNow_ && !rs.rawPlayback) (void)adapt().update(stats, caps_.thermal());
    // A luz de ambiente termina em segundo plano. Mesmo parado, precisamos
    // apresentar outro quadro para que metais recebam os reflexos prontos.
    refinePending_ = !playing && ((!snapshot_.scenes.empty() && renderer_.environment_pending())
        || (!refineNow_ && (rs.previewDenominator > adapt().still_denominator(caps_.thermal())
            || adapt().state().heavyLevel > adapt().still_heavy_level(caps_.thermal()))));
    refineDueNs_ = refinePending_ ? monotonic_ns() + 250'000'000ull : 0;
    media_.collect(frameCounter_);
    if (frameCounter_ % 120 == 0) (void)memory_.balance();
    update_perf(stats, timings, snapshot_, frameStart);
    return s;
}

void Engine::set_thermal(u32 level, bool throttling) noexcept {
    ThermalState t = caps_.thermal();
    const ThermalTier before = t.tier();
    t.level = static_cast<ThermalState::Level>(std::min<u32>(level, static_cast<u32>(ThermalState::Level::Unknown)));
    t.throttling = throttling;
    caps_.set_thermal_state(t);
    thermalDegrade_.store(t.should_degrade(), std::memory_order_release);
    // Morno: menos tarefa de fundo; crítico: metade do pool (§33, §35).
    jobs_.apply_thermal(static_cast<u32>(t.level));
    // A política térmica (DevicePolicy, §35–36) nos knobs que existem hoje:
    // o custo das operações caras do preview sai de preview_heavy_scale() no
    // próximo quadro; o trabalho de fundo (miniaturas) ganha pausa. O export
    // não lê nada disto (§37).
    const DevicePolicy p = caps_.policy();
    thumbs_.set_pacing_ms(p.backgroundPauseMs);
    media_.proxies().set_policy(p.preferProxyAboveShortSide, p.backgroundPauseMs);
    media_.proxies().set_pause_reason(PreviewProxyService::Thermal, t.severe());
    if (p.thermal != before) {
        AUREA_LOG_INFO("termico: %s -> %s (preview x%.2f, fundo %u ms)", thermal_tier_name(before),
                       thermal_tier_name(p.thermal), static_cast<double>(p.heavyScale), p.backgroundPauseMs);
    }
    request_render();
}

u32 Engine::thermal_heavy_level() const noexcept {
    const ThermalState& t = caps_.thermal();
    return t.severe() ? 2u : t.should_degrade() ? 1u : 0u;
}

f32 Engine::preview_heavy_scale() const noexcept {
    // O menor entre a política do aparelho × temperatura (8H) e a decisão do
    // AUTO 2.0 (8C).
    return std::min(caps_.policy().heavyScale, adaptive_->quality().heavy());
}

Status Engine::render_offscreen(TextureHandle target, u32 width, u32 height, bool asPreview) noexcept {
    // O lock de render é pego só por PREPARE + RENDER (curto). A espera pelos
    // frames de vídeo (até 4 s num arquivo quebrado) fica FORA dele: com o
    // lock preso, `detach_surface` (surfaceDestroyed na thread principal) e a
    // thread de render esperavam a captura da miniatura — no Galaxy S22 isso
    // deu ANR ("Input dispatching timed out") ao sair do editor.
    std::unique_lock<std::mutex> rl(renderMutex_);
    if (!gpu_ || !renderer_.ready()) return Status{Errc::InvalidState, "sem GPU"};

    RenderSettings rs;
    rs.dither = false;
    rs.gpuTimers = offscreenTimers_;
    if (asPreview) rs.heavyScale = preview_heavy_scale();
    OffscreenMeasure m;
    const u64 tStart = monotonic_ns();
    u64 tPrep0 = tStart, tPrep1 = tStart;
    // Espera os frames EXATOS de vídeo (export e teste não aceitam o frame
    // aproximado que o scrub mostra). Limite de 4 s para arquivo quebrado não
    // travar para sempre.
    for (int attempt = 0; attempt < 800; ++attempt) {
        if (!rl.owns_lock()) rl.lock();
        if (!gpu_ || !renderer_.ready()) return Status{Errc::InvalidState, "sem GPU"};
        tPrep0 = monotonic_ns();
        ++m.mediaAttempts;
        {
            std::lock_guard<std::mutex> lock(modelMutex_);
            if (!project_) return Errc::InvalidState;
            drain_commands_locked();
            Composition* comp = current_composition();
            if (!comp) return Errc::NotFound;
            playback_.configure(comp->fps(), comp->duration());
            const FrameIndex t = playback_.update(monotonic_ns());
            project_->timeline().set_playhead(t);
            renderer_.prepare(*comp, *project_, t, &media_, &Engine::image_lookup, this, rs, ++frameCounter_,
                              0, DecodeMode::Still, 1.0f, snapshot_);
        }
        tPrep1 = monotonic_ns();
        if (snapshot_.missingVideoFrames == 0 && snapshot_.staleVideoFrames == 0) break;
        for (RenderLayer& l : snapshot_.layers) l.source.frame.reset();
        // Dorme SEM o lock: quem precisa da GPU/superfície nesse meio-tempo
        // (detach, render do preview) passa na frente.
        rl.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!rl.owns_lock()) rl.lock();
    if (!gpu_ || !renderer_.ready()) return Status{Errc::InvalidState, "sem GPU"};
    auto ms = [](u64 a, u64 b) { return static_cast<f32>(static_cast<f64>(b - a) * 1e-6); };
    m.prepareMs = ms(tPrep0, tPrep1);
    m.mediaWaitMs = ms(tStart, tPrep0);
    for (const EffectPlan& p : snapshot_.plans) m.activeEffects += static_cast<u32>(p.evals.size());
    for (const RenderLayer& l : snapshot_.layers) {
        if (l.source.kind == LayerSource::Kind::Particles) m.particles += l.source.particleSlots;
    }

    OffscreenTarget off{target, width, height};
    FrameStats stats;
    RenderTimings timings;
    const Status s = renderer_.render(snapshot_, rs, &off, stats, timings);
    const u64 tW0 = monotonic_ns();
    gpu_->wait_idle();
    m.gpuWaitMs = ms(tW0, monotonic_ns());
    m.recordMs = timings.cpuRecordMs;
    m.submitMs = timings.presentMs;
    m.passesExecuted = stats.passesExecuted;
    m.passesCulled = stats.passesCulled;
    m.drawCalls = stats.drawCalls;
    m.layersRendered = stats.layersRendered;
    if (!snapshot_.scenes.empty()) {   // sem cena o SceneStats é o do último quadro 3D
        const scene3d::SceneStats& ss = renderer_.scene_stats();
        m.draws3D = ss.drawCalls;
        m.triangles3D = ss.triangles;
        m.culled3D = ss.culledPrimitives;
    }
    m.transientBytes = renderer_.graph_stats().transientBytes;
    m.gpuUsedBytes = gpu_->memory_stats().usedBytes;
    if (offscreenTimers_ && s.ok()) {
        // Depois do wait_idle o backend já leu os timestamps DESTE quadro.
        f32 total = 0.0f;
        m.gpuPasses = gpu_->read_gpu_timings(offscreenPasses_, 64, &total);
        m.gpuMeasured = m.gpuPasses > 0;
        m.gpuMs = m.gpuMeasured ? total : 0.0f;
    }
    offscreenMeasure_ = m;
    return s;
}

u32 Engine::last_offscreen_gpu_passes(GpuTiming* out, u32 capacity) const noexcept {
    const u32 n = std::min(capacity, offscreenMeasure_.gpuPasses);
    for (u32 i = 0; i < n; ++i) out[i] = offscreenPasses_[i];
    return n;
}

bool Engine::set_effect_preview_source(const u8* rgba, u32 width, u32 height) noexcept {
    if (!rgba || width == 0 || height == 0 || width > 4096 || height > 4096) return false;
    std::vector<u8> px(rgba, rgba + static_cast<usize>(width) * height * 4);
    std::lock_guard<std::mutex> rl(renderMutex_);
    renderer_.set_effect_preview_source(std::move(px), width, height);
    return true;
}

Status Engine::render_effect_preview(u32 typeId, u32 width, u32 height, std::vector<u8>& out, u32& outWidth,
                                     u32& outHeight) noexcept {
    if (!gpu_ || !renderer_.ready()) return Status{Errc::InvalidState, "sem GPU"};
    if (width == 0 || height == 0) return Status{Errc::InvalidArgument, "previa sem tamanho"};
    // A cartela é pequena, mas o aparelho manda: nunca pedir textura maior do
    // que ele cria — e, no aparelho de entrada, prévia com metade do lado
    // (DevicePolicy::effectPreviewMaxSide). Passando do teto, encolhe pela
    // proporção (nunca estica); a UI escala o bitmap no cartão.
    const u32 maxTex = std::min<u32>(gpu_->capabilities().maxTexture2D, std::max<u32>(16, caps_.policy().effectPreviewMaxSide));
    u32 w = width, h = height;
    if (w > maxTex || h > maxTex) {
        const f32 k = static_cast<f32>(maxTex) / static_cast<f32>(std::max(w, h));
        w = std::max<u32>(1, static_cast<u32>(static_cast<f32>(w) * k));
        h = std::max<u32>(1, static_cast<u32>(static_cast<f32>(h) * k));
    }
    std::lock_guard<std::mutex> rl(renderMutex_);
    const Status s = renderer_.render_effect_preview(effectRegistry_, typeId, w, h, out);
    if (s.ok()) {
        outWidth = w;
        outHeight = h;
    }
    return s;
}

Status Engine::capture_frame_rgba(u32 maxDim, std::vector<u8>& out, u32& width, u32& height) noexcept {
    if (!gpu_ || maxDim == 0) return Status{Errc::InvalidState, "sem GPU"};
    u32 cw = 0, ch = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        const Composition* c = current_composition();
        if (!c) return Errc::InvalidState;
        cw = c->width();
        ch = c->height();
    }
    if (cw == 0 || ch == 0) return Errc::InvalidState;
    const f64 k = static_cast<f64>(maxDim) / static_cast<f64>(std::max(cw, ch));
    width = std::max<u32>(1, static_cast<u32>(std::lround(cw * std::min(1.0, k))));
    height = std::max<u32>(1, static_cast<u32>(std::lround(ch * std::min(1.0, k))));

    TextureDesc d;
    d.width = width;
    d.height = height;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    d.debugName = "captura-do-projeto";
    auto target = gpu_->create_texture(d);
    if (!target.ok()) return target.status();
    std::vector<u16> half(static_cast<usize>(width) * height * 4);
    Status s = render_offscreen(*target, width, height);
    if (s.ok()) s = gpu_->read_texture(*target, half.data(), width * 8);
    gpu_->destroy_texture(*target);
    if (!s.ok()) return s;

    auto h2f = [](u16 h) noexcept {
        const u32 sign = (h & 0x8000u) << 16, exp = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
        u32 bits;
        if (exp == 0) {
            if (mant == 0) bits = sign;
            else {
                u32 e = 113, m = mant;
                while (!(m & 0x400u)) { m <<= 1; --e; }
                bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
            }
        } else if (exp == 31) {
            bits = sign | 0x7F800000u | (mant << 13);
        } else {
            bits = sign | ((exp + 112) << 23) | (mant << 13);
        }
        f32 f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    auto encode = [](f32 v) noexcept {
        v = std::clamp(v, 0.0f, 1.0f);
        const f32 e = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        return static_cast<u8>(std::lround(std::clamp(e, 0.0f, 1.0f) * 255.0f));
    };
    out.resize(static_cast<usize>(width) * height * 4);
    for (usize i = 0; i < static_cast<usize>(width) * height; ++i) {
        const f32 a = h2f(half[i * 4 + 3]);
        const f32 inv = a > 1e-5f ? 1.0f / a : 0.0f;   // trabalho é pré-multiplicado
        out[i * 4 + 0] = encode(h2f(half[i * 4 + 0]) * inv);
        out[i * 4 + 1] = encode(h2f(half[i * 4 + 1]) * inv);
        out[i * 4 + 2] = encode(h2f(half[i * 4 + 2]) * inv);
        out[i * 4 + 3] = static_cast<u8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
    }
    return OkStatus;
}

// =============================================================================
// Status, telemetria e painel DEV
// =============================================================================
void Engine::update_perf(const FrameStats& stats, const RenderTimings& t, const FrameSnapshot& snap,
                         u64 frameStartNs) noexcept {
    ++fpsWindowFrames_;
    if (fpsWindowStartNs_ == 0) fpsWindowStartNs_ = frameStartNs;
    // Ritmo: só quadros seguidos TOCANDO contam (parado, o intervalo é o
    // tempo entre toques da pessoa, não ritmo de quadro).
    if (playingHint_.load(std::memory_order_relaxed) && lastPresentNs_ != 0) {
        pacingMs_[pacingHead_] = static_cast<f32>(static_cast<f64>(frameStartNs - lastPresentNs_) * 1e-6);
        pacingHead_ = (pacingHead_ + 1) % kPacingRing;
        if (pacingCount_ < kPacingRing) ++pacingCount_;
    }
    lastPresentNs_ = playingHint_.load(std::memory_order_relaxed) ? frameStartNs : 0;
    const u64 elapsed = frameStartNs - fpsWindowStartNs_;
    if (elapsed >= 1'000'000'000ull) {
        measuredFps_ = static_cast<f32>(static_cast<f64>(fpsWindowFrames_) * 1e9 / static_cast<f64>(elapsed));
        fpsWindowFrames_ = 0;
        fpsWindowStartNs_ = frameStartNs;
        frameScheduler_.roll_window();
        roll_pacing();
    }

    const MediaManager::Stats ms = media_.stats();
    const FrameGraph::Stats& gs = renderer_.graph_stats();
    const TransientTexturePool::Stats& ps = renderer_.pool_stats();

    bridge::PerfPOD p{};
    p.previewFps = measuredFps_;
    p.cpuFrameMs = t.cpuPrepareMs + t.cpuRecordMs;
    p.gpuFrameMs = t.gpuMeasured ? t.gpuTotalMs : 0.0f;
    p.decodeMs = ms.decodeMsAvg;
    p.colorConvMs = t.gpuColorConvMs;
    p.effectsMs = t.gpuEffectsMs;
    p.blurMs = t.gpuBlurMs;
    p.glowMs = t.gpuGlowMs;
    p.compositeMs = t.gpuCompositeMs;
    p.outputMs = t.gpuOutputMs;
    p.presentMs = t.presentMs;
    p.acquireMs = t.acquireWaitMs;
    p.lastSeekMs = ms.lastSeekMs;
    p.frameBudgetMs = adapt().budget().totalMs;
    p.droppedFrames = frameScheduler_.dropped_total();
    p.droppedRecent = frameScheduler_.dropped_recent();
    p.renderScaleNum = adapt().current_numerator();
    p.renderScaleDen = adapt().current_denominator();
    p.renderAuto = adapt().auto_mode() ? 1u : 0u;
    p.previewWidth = stats.previewWidth;
    p.previewHeight = stats.previewHeight;
    p.decodedCacheFrames = ms.cachedFrames;
    p.decodedCacheBytes = ms.cachedBytes;
    p.ramBytes = memory_.total_used();
    p.gpuMemoryBytes = gpu_ ? gpu_->memory_stats().usedBytes : 0;
    p.transientBytes = gs.transientBytes;
    p.passesExecuted = gs.passesExecuted;
    p.passesCulled = gs.passesCulled;
    p.texturesCreated = ps.createdThisFrame;
    p.transientTextures = gs.transientTextures;
    p.physicalTextures = gs.physicalTextures;
    p.aliasedTextures = gs.aliasedTextures;
    p.pipelineCompilesLive = renderer_.shaders().compiles_since_mark();
    p.pipelinesTotal = renderer_.shaders().pipeline_count();
    p.zeroCopy = renderer_.last_frame_zero_copy() ? 1u : 0u;
    p.hardwareDecoder = ms.hardwareDecoder ? 1u : 0u;
    p.gpuTimers = t.gpuMeasured ? 1u : 0u;
    p.seeks = static_cast<u32>(ms.seeks);
    p.coalesced = static_cast<u32>(ms.coalesced);
    p.staleFrames = snap.staleVideoFrames;
    p.layersRendered = stats.layersRendered;
    p.thermal = static_cast<u32>(caps_.thermal().level);
    std::snprintf(p.decoder, sizeof(p.decoder), "%s", ms.decoderName);
    if (gpu_) std::snprintf(p.gpuName, sizeof(p.gpuName), "%s", gpu_->capabilities().deviceName.c_str());

    // Fase 8A: contadores que já existem, copiados (nada alocado, nada
    // estimado — o que o motor não mede fica 0 e a HUD não mostra).
    p.pacingP50Ms = pacingP50_;
    p.pacingP95Ms = pacingP95_;
    p.pacingP99Ms = pacingP99_;
    p.pacingStdMs = pacingStd_;
    p.pacingSamples = pacingSamples_;
    p.cpuPrepareMs = t.cpuPrepareMs;
    p.cpuRecordMs = t.cpuRecordMs;
    p.drawCalls = stats.drawCalls;
    if (!snap.scenes.empty()) {   // sem cena, o SceneStats é o do último quadro 3D
        const scene3d::SceneStats& ss = renderer_.scene_stats();
        p.draws3D = ss.drawCalls;
        p.triangles3D = ss.triangles;
        p.culled3D = ss.culledPrimitives;
    }
    for (const EffectPlan& plan : snap.plans) p.activeEffects += static_cast<u32>(plan.evals.size());
    for (const RenderLayer& l : snap.layers) {
        if (l.source.kind == LayerSource::Kind::Particles) p.particles += l.source.particleSlots;
    }
    renderer_.flow_cache_stats(p.flowCacheHits, p.flowCacheMisses);
    renderer_.mask_cache_stats(p.maskCacheHits, p.maskCacheMisses);
    const audio::AudioEngine::Stats as = audio_.stats();
    p.audioOutputOpen = as.outputOpen ? 1u : 0u;
    if (as.outputOpen) {
        p.audioQueuedMs = as.queuedMs;
        p.audioUnderruns = static_cast<u32>(as.underruns);
        p.audioMissingBlocks = static_cast<u32>(as.missingBlocks);
        if (config_.audioOutput) p.audioOutputMs = config_.audioOutput->latency_frames() * 1000u / audio::kMixRate;
    }
    p.heavyScale = preview_heavy_scale();
    p.memoryBudgetMB = static_cast<u32>(memory_.total_budget() >> 20);
    p.scene3dBytes = renderer_.scene_resident_bytes();
    if (gpu_) {
        const GpuMemoryStats gm = gpu_->memory_stats();
        p.gpuReservedBytes = gm.reservedBytes;
        p.gpuAllocations = gm.allocationCount;
    }

    // A partir do primeiro frame mostrado, pipeline novo é "durante o
    // playback" — o número certo no painel é zero.
    if (renderer_.frames_rendered() == 1) renderer_.shaders().mark_steady_state();

    std::lock_guard<std::mutex> lock(perfMutex_);
    perf_ = p;
    lastFrame_ = stats;
    lastFrame_.gpuMs = p.gpuFrameMs;
}

void Engine::roll_pacing() noexcept {
    // Uma vez por segundo, na thread de render: cópia na pilha (128 floats) e
    // nth_element — nada alocado. Menos de 8 amostras = não tocou o bastante.
    pacingSamples_ = pacingCount_;
    if (pacingCount_ < 8) {
        pacingP50_ = pacingP95_ = pacingP99_ = pacingStd_ = 0.0f;
        pacingCount_ = pacingHead_ = 0;
        return;
    }
    f32 v[kPacingRing];
    const u32 n = pacingCount_;
    f64 sum = 0.0, sum2 = 0.0;
    for (u32 i = 0; i < n; ++i) {
        v[i] = pacingMs_[i];
        sum += v[i];
        sum2 += static_cast<f64>(v[i]) * v[i];
    }
    auto pct = [&](f64 q) {
        const u32 k = std::min<u32>(n - 1, static_cast<u32>(q * static_cast<f64>(n - 1) + 0.5));
        std::nth_element(v, v + k, v + n);
        return v[k];
    };
    pacingP50_ = pct(0.50);
    pacingP95_ = pct(0.95);
    pacingP99_ = pct(0.99);
    const f64 mean = sum / n;
    pacingStd_ = static_cast<f32>(std::sqrt(std::max(0.0, sum2 / n - mean * mean)));
    pacingCount_ = pacingHead_ = 0;   // janela nova: o número é do último segundo
}

void Engine::fill_perf(bridge::PerfPOD& out) noexcept {
    std::lock_guard<std::mutex> lock(perfMutex_);
    out = perf_;
}

EngineStatus Engine::read_status() noexcept {
    EngineStatus st;
    st.state = state_;
    st.lastError = lastError_;
    std::snprintf(st.lastErrorDetail, sizeof(st.lastErrorDetail), "%s", lastErrorDetail_);
    {
        std::lock_guard<std::mutex> lock(perfMutex_);
        st.averageFrameMs = lastFrame_.cpuMs;
        st.gpuMs = lastFrame_.gpuMs;
        st.cpuMs = lastFrame_.cpuMs;
        st.decodeMs = lastFrame_.decodeMs;
        st.passesExecuted = lastFrame_.passesExecuted;
        st.passesCulled = lastFrame_.passesCulled;
        st.gpuMemoryBytes = lastFrame_.gpuMemoryBytes;
        st.cpuMemoryBytes = lastFrame_.cpuMemoryBytes;
        st.previewWidth = lastFrame_.previewWidth;
        st.previewHeight = lastFrame_.previewHeight;
        st.currentFps = perf_.previewFps;
        st.droppedFrames = perf_.droppedFrames;
    }
    st.memoryPressure = memory_.pressure();

    std::lock_guard<std::mutex> lock(modelMutex_);
    st.previewNumerator = adapt().current_numerator();
    st.previewDenominator = adapt().current_denominator();
    st.previewAuto = adapt().auto_mode();
    if (project_) {
        st.playhead = playback_.current();
        st.playing = playback_.playing();
        st.assetCount = project_->asset_count();
        st.dirty = project_->dirty();
        st.recoveryAvailable = project_->autosave().recoveryAvailable;
        if (const Composition* c = project_->timeline().composition(project_->timeline().current())) {
            st.duration = c->duration();
            st.layerCount = c->layers().count();
            st.compFps = c->fps();
            st.compWidth = c->width();
            st.compHeight = c->height();
        }
    }
    st.canUndo = history_.can_undo();
    st.canRedo = history_.can_redo();
    st.undoDepth = history_.depth();
    st.selectedCount = static_cast<u32>(selection_.size());
    return st;
}

EngineTelemetry Engine::read_telemetry() noexcept {
    EngineTelemetry t;
    {
        std::lock_guard<std::mutex> lock(perfMutex_);
        t.frame = lastFrame_;
    }
    t.workerCount = jobs_.worker_count();
    t.jobsCompleted = jobs_.completed_count();
    for (u8 i = 0; i < static_cast<u8>(JobPriority::Count); ++i) {
        t.queueDepth[i] = jobs_.queue_depth(static_cast<JobPriority>(i));
    }
    t.pipelineCount = renderer_.shaders().pipeline_count();
    t.shaderCount = kShaderCount;
    t.shaderFailures = renderer_.shaders().compile_failures();
    t.physicalResources = renderer_.graph_stats().physicalTextures;
    t.logicalResources = renderer_.graph_stats().transientTextures;
    t.adaptiveScaleChanges = adaptive_ ? adaptive_->change_count() : 0;
    if (adaptive_) {
        t.previewDenominator = adaptive_->current_denominator();
        t.previewHeavyLevel = adaptive_->state().heavyLevel;
        t.previewBottleneck = adaptive_->state().bottleneck;
        t.previewUpBackoff = adaptive_->state().upBackoff;
    }
    t.culledLayers = lastCulledLayers_.load(std::memory_order_relaxed);
    {
        // Snapshots de composição do desfazer, estimados (History::bytes).
        std::lock_guard<std::mutex> lock(modelMutex_);
        t.undoBlobBytes = history_.bytes();
    }
    t.commandsDropped = commandQueue_->dropped_count();
    t.thermal = caps_.thermal().level;
    t.throttling = caps_.thermal().throttling;
    // Cache de quadros decodificados (§15): acerto somado de todas as fontes.
    CacheMetrics m[MemoryManager::kMaxReclaimables];
    const u32 n = memory_.collect_metrics(m, MemoryManager::kMaxReclaimables);
    u64 hits = 0, misses = 0;
    for (u32 i = 0; i < n; ++i) {
        if (m[i].cls != MemoryClass::DecodedFrames) continue;
        hits += m[i].hits;
        misses += m[i].misses;
        t.frameCacheEntries += m[i].entries;
    }
    t.frameCacheHitRate = hits + misses ? static_cast<f32>(static_cast<f64>(hits) / static_cast<f64>(hits + misses)) : 0.0f;
    return t;
}

void Engine::debug_feed_frame_stats(const FrameStats& stats) noexcept {
    {
        std::lock_guard<std::mutex> lock(perfMutex_);
        lastFrame_ = stats;
    }
    if (adaptive_) (void)adaptive_->update(stats, caps_.thermal());
}

Composition* Engine::current_composition() noexcept {
    if (!project_) return nullptr;
    return project_->timeline().composition(project_->timeline().current());
}

// =============================================================================
// Consultas para a UI
// =============================================================================
u32 Engine::query_layers(bridge::LayerRow* out, u32 capacity, char* outNameBlob,
                         u32 nameBlobCapacity) noexcept {
    if (!out) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = current_composition();
    if (!comp) return 0;

    u32 written = 0;
    u32 nameCursor = 0;
    bool hasParents = false;
    // A UI mostra a FRENTE em cima: a frente é a última da ordem.
    const u32 n = comp->order().size();
    for (u32 i = 0; i < n && written < capacity; ++i) {
        const LayerId id = comp->order().at(n - 1 - i);
        const Layer* l = comp->layer(id);
        if (!l) continue;

        bridge::LayerRow row;
        row.id = id.pack();
        row.kind = static_cast<u32>(l->kind);
        row.zIndex = i;
        row.startFrame = static_cast<i32>(l->start.value);
        row.endFrame = static_cast<i32>(l->end.value);
        row.offsetFrames = static_cast<i32>(l->offset.value);
        row.trackId = l->trackId;   // a LINHA: a UI junta na mesma fileira quem a divide
        row.opacity = l->transform.opacity;
        row.effectCount = static_cast<u32>(l->effects.size());
        row.maskCount = static_cast<u32>(l->masks.size());
        u32 keyCount = static_cast<u32>(l->timeRemap.keys.size());
        for (u32 t = 0; t < l->tracks.size(); ++t) keyCount += static_cast<u32>(l->tracks.at(t).keys.size());
        row.keyframeCount = keyCount;
        row.blendMode = static_cast<u32>(l->blendMode);
        u32 flags = 0;
        if (l->visible) flags |= bridge::kLayerRowFlagVisible;
        if (l->locked)  flags |= bridge::kLayerRowFlagLocked;
        if (l->solo)    flags |= bridge::kLayerRowFlagSolo;
        if (l->animated()) flags |= bridge::kLayerRowFlagAnimated;
        if (std::binary_search(selection_.begin(), selection_.end(), row.id)) flags |= bridge::kLayerRowFlagSelected;
        if (l->threeD)  flags |= bridge::kLayerRowFlagThreeD;
        if (l->adjustment) flags |= bridge::kLayerRowFlagAdjustment;
        if (l->guide)   flags |= bridge::kLayerRowFlagGuide;
        if (!l->captions.empty()) flags |= bridge::kLayerRowFlagCaptions;
        if (l->magneticTrack) flags |= bridge::kLayerRowFlagMagnetic;
        flags |= (static_cast<u32>(l->label) << bridge::kLayerRowLabelShift) & bridge::kLayerRowLabelMask;
        row.flags = flags;
        row.parentIndex = kInvalidIndex;
        hasParents |= l->parent.valid();
        if (outNameBlob && nameCursor + l->name.size() < nameBlobCapacity) {
            std::memcpy(outNameBlob + nameCursor, l->name.data(), l->name.size());
            row.nameOffset = nameCursor;
            row.nameLength = static_cast<u32>(l->name.size());
            nameCursor += static_cast<u32>(l->name.size());
        }
        out[written++] = row;
    }
    // The UI indexes the returned, front-to-back rows (not pool slots or
    // composition order). Resolve only after all rows exist; a parent may be
    // below its child or outside the caller's truncated buffer.
    if (hasParents) {
        std::unordered_map<u64, u32> rowById;
        rowById.reserve(written);
        for (u32 i = 0; i < written; ++i) rowById.emplace(out[i].id, i);
        for (u32 i = 0; i < written; ++i) {
            const Layer* layer = comp->layer(LayerId::unpack(out[i].id));
            if (!layer || !layer->parent.valid()) continue;
            const auto parent = rowById.find(layer->parent.pack());
            if (parent != rowById.end()) out[i].parentIndex = parent->second;
        }
    }
    return written;
}

namespace {
/// Linhas de keyframe de uma camada (trilha a trilha), no máximo `capacity`.
u32 write_keyframe_rows(const Layer& l, bridge::KeyframeRow* out, u32 capacity) noexcept {
    u32 written = 0;
    auto put = [&](const Track& track) {
        for (const Keyframe& k : track.keys) {
            if (written >= capacity) break;
            bridge::KeyframeRow row;
            row.property = static_cast<u32>(track.property);
            row.effectIndex = track.effectIndex;
            row.time = static_cast<i32>(k.time.value);
            row.value = k.value;
            row.interpolation = static_cast<u32>(k.interp);
            row.paramIndex = track.effectParamIndex;
            out[written++] = row;
        }
    };
    put(l.timeRemap);
    for (u32 t = 0; t < l.tracks.size() && written < capacity; ++t) put(l.tracks.at(t));
    return written;
}
} // namespace

u32 Engine::query_keyframes(u64 layerId, bridge::KeyframeRow* out, u32 capacity) noexcept {
    if (!out) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = current_composition();
    if (!comp) return 0;
    Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return 0;
    return write_keyframe_rows(*l, out, capacity);
}

u32 Engine::query_all_keyframes(bridge::KeyframeIndexRow* outIndex, u32 layerCapacity,
                                bridge::KeyframeRow* out, u32 capacity, u32* outLayers) noexcept {
    if (outLayers) *outLayers = 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = current_composition();
    if (!comp) return 0;
    const u32 n = comp->order().size();
    // 1ª passada: quanto espaço precisa (sem escrever nada se não couber).
    u32 layers = 0;
    u64 total = 0;
    for (u32 i = 0; i < n; ++i) {
        const Layer* l = comp->layer(comp->order().at(n - 1 - i));
        if (!l) continue;
        ++layers;
        total += l->timeRemap.keys.size();
        for (u32 t = 0; t < l->tracks.size(); ++t) total += l->tracks.at(t).keys.size();
    }
    if (outLayers) *outLayers = layers;
    const u32 totalU = static_cast<u32>(std::min<u64>(total, 0xFFFFFFFFull));
    if (!outIndex || !out || layers > layerCapacity || total > capacity) return totalU;
    u32 li = 0, cursor = 0;
    for (u32 i = 0; i < n; ++i) {
        const LayerId id = comp->order().at(n - 1 - i);
        const Layer* l = comp->layer(id);
        if (!l) continue;
        const u32 written = write_keyframe_rows(*l, out + cursor, capacity - cursor);
        outIndex[li].layerId = id.pack();
        outIndex[li].count = written;
        outIndex[li].reserved = 0;
        ++li;
        cursor += written;
    }
    return totalU;
}

u32 Engine::query_waveform(u64 layerId, f64 startFrame, f64 framesPerBucket, u32 count, u8* out) noexcept {
    if (!out || count == 0 || !waveforms_ || !(framesPerBucket > 0.0)) return 0;
    u64 key = 0;
    f64 srcStart = 0.0, perBucket = 0.0;
    bool reversedWave = false;
    std::optional<Layer> remapCopy;   // curva lida fora do lock
    const Layer* remapLayer = nullptr;
    f64 remapFps = 30.0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = current_composition();
        if (!comp || !project_) return 0;
        const Layer* l = comp->layer(LayerId::unpack(layerId));
        if (!l || (l->kind != LayerKind::Video && l->kind != LayerKind::Audio)) return 0;
        const Asset* a = project_->asset(l->source);
        if (!a || !a->has_audio()) return 0;
        key = l->source.pack();
        const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        if (l->timeRemapEnabled && !l->timeRemap.keys.empty()) {
            remapCopy.emplace();
            remapCopy->start = l->start;
            remapCopy->end = l->end;
            remapCopy->offset = l->offset;
            remapCopy->timeRemap = l->timeRemap;
            remapCopy->timeRemapEnabled = true;
            remapLayer = &*remapCopy;
            remapFps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        } else if (const Track* st = l->speed_track()) {
            // Velocidade com keyframes: a mesma integral do vídeo, balde a balde.
            remapCopy.emplace();
            remapCopy->start = l->start;
            remapCopy->end = l->end;
            remapCopy->offset = l->offset;
            remapCopy->reversed = l->reversed;
            remapCopy->speed = l->speed;
            remapCopy->tracks.get_or_create(TrackProperty::Speed) = *st;
            remapLayer = &*remapCopy;
            remapFps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        } else if (l->speed <= 0.0f) {
            return 0;   // quadro congelado: sem som
        }
        // Frame da timeline → amostra da fonte pela MESMA conta do vídeo e do
        // mixer (velocidade e reverso incluídos).
        const f64 rate = static_cast<f64>(l->speed);
        const f64 atStart = l->source_frame(l->start);
        const f64 dir = l->reversed ? -1.0 : 1.0;
        srcStart = (atStart + dir * (startFrame - static_cast<f64>(l->start.value)) * rate) * audio::kMixRate / fps;
        perBucket = framesPerBucket * rate * audio::kMixRate / fps;
        reversedWave = l->reversed;
        i64 len = a->audio.sampleCount.value > 0 && a->audio.sampleRate > 0
                ? a->audio.sampleCount.value * static_cast<i64>(audio::kMixRate) / a->audio.sampleRate
                : audio::frame_to_sample(a->duration.value, a->timebaseFps > 0.0 ? a->timebaseFps : fps);
        waveforms_->request(key, audio::AudioAssetRef{resolve_asset_path(a->sourcePath), len});
    }
    if (remapLayer) {
        // Curva de tempo: cada balde pede o seu trecho da fonte.
        for (u32 b = 0; b < count; ++b) {
            const f64 a = remapLayer->source_frame_f(startFrame + b * framesPerBucket) * audio::kMixRate / remapFps;
            const f64 z = remapLayer->source_frame_f(startFrame + (b + 1) * framesPerBucket) * audio::kMixRate / remapFps;
            u8 v = 0;
            if (!waveforms_->query(key, std::min(a, z), std::max(1.0, std::fabs(z - a)), 1, &v)) return 0;
            out[b] = v;
        }
        return count;
    }
    if (!reversedWave) return waveforms_->query(key, srcStart, perBucket, count, out) ? count : 0;
    // Reverso: a fonte anda para trás — pede o trecho em ordem e inverte.
    const f64 lo = srcStart - perBucket * count;
    if (!waveforms_->query(key, lo, perBucket, count, out)) return 0;
    std::reverse(out, out + count);
    return count;
}

u32 Engine::query_thumbnail(u64 layerId, i32 timelineFrame, u32 height, u8* out, u32 capacity,
                            u32* outWidth) noexcept {
    if (!out || height == 0 || height > 512) return 0;
    ThumbnailService::Image img;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = current_composition();
        if (!comp || !project_) return 0;
        const Layer* l = comp->layer(LayerId::unpack(layerId));
        if (!l || !l->source.valid()) return 0;
        const Asset* a = project_->asset(l->source);
        if (!a) return 0;
        const u64 key = l->source.pack();
        if (a->kind == AssetKind::Image) {
            const auto it = images_.find(key);
            if (it == images_.end()) return 0;
            if (!thumbs_.image(key, it->second.rgba.data(), it->second.width, it->second.height, height, img)) return 0;
        } else if (a->kind == AssetKind::Video) {
            const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
            const f64 src = std::max(0.0, l->source_frame(FrameIndex{timelineFrame}));
            const i64 us = static_cast<i64>(std::llround(src * 1e6 / fps));
            if (!thumbs_.video(key, *a, us, height, img)) return 0;
        } else {
            return 0;
        }
    }
    const u32 bytes = static_cast<u32>(img.rgba.size());
    if (bytes > capacity) return 0;
    std::memcpy(out, img.rgba.data(), bytes);
    if (outWidth) *outWidth = img.width;
    return bytes;
}

bool Engine::query_composition(u64& id, u32& width, u32& height, f64& fps, i64& durationFrames,
                               f32 background[4]) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return false;
    const CompositionId cid = project_->timeline().current();
    const Composition* c = project_->timeline().composition(cid);
    if (!c) return false;
    id = cid.pack();
    width = c->width();
    height = c->height();
    fps = c->fps();
    durationFrames = c->duration().value;
    const Color bg = c->background();
    background[0] = bg.r;
    background[1] = bg.g;
    background[2] = bg.b;
    background[3] = bg.a;
    return true;
}

void Engine::composition_size_cap(u32& longSide, u32& shortSide) const noexcept {
    longSide = std::max(caps_.max_export_width(), caps_.max_export_height());
    shortSide = std::min(caps_.max_export_width(), caps_.max_export_height());
}

namespace {
/// Caixa do conteúdo da camada vetorial no instante (px da camada). Sem
/// conteúdo desenhável, a tela da camada (para o palco ainda mostrar e tocar).
Rect vector_content_box(const Layer& l, FrameIndex local) {
    std::vector<VectorGroup> ev;
    ev.reserve(l.shape.vector.groups.size());
    for (u32 gi = 0; gi < l.shape.vector.groups.size(); ++gi)
        ev.push_back(vector::evaluate_group(l.shape.vector.groups[gi], l.tracks, gi, static_cast<f64>(local.value)));
    Vec2 mn, mx;
    if (vector::bounds_of(ev, static_cast<f64>(local.value), mn, mx) && mx.x - mn.x >= 1.0f && mx.y - mn.y >= 1.0f)
        return Rect{mn.x, mn.y, mx.x - mn.x, mx.y - mn.y};
    return l.shape.bounds;
}
} // namespace

bool Engine::query_layer_detail(u64 layerId, bridge::LayerDetailPOD& out) noexcept {
    out = bridge::LayerDetailPOD{};
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!fill_layer_detail_locked(layerId, out)) return false;
    // Geometria no palco: cantos e pai, pelo MESMO cálculo do renderer.
    const Composition* comp = current_composition();
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return true;
    const FrameIndex now = playback_.current();
    const Layer* p = l->parent.valid() ? comp->layer(l->parent) : nullptr;
    if (p || l->hasParentBasis) {
        const Mat4 pm = (p ? (wants_layer_3d(*comp, *l, now) ? layer_world_3d(*comp, *p, now) : layer_world_matrix(*comp, *p, now)) : Mat4::identity()) * l->parentBasis;
        out.parentAffine[0] = pm.col[0].x; out.parentAffine[1] = pm.col[0].y;
        out.parentAffine[2] = pm.col[1].x; out.parentAffine[3] = pm.col[1].y;
        out.parentAffine[4] = pm.col[3].x; out.parentAffine[5] = pm.col[3].y;
    }
    const bool boxed = out.sourceWidth > 0 && out.sourceHeight > 0 && l->kind != LayerKind::Model3D
                    && l->kind != LayerKind::Camera && l->kind != LayerKind::Light;
    if (boxed) {
        bool persp = false;
        Mat4 m = layer_comp_matrix(*comp, *l, now, &persp);
        // Cena 3D: o palco mostra a câmera de navegação, então os cantos (que
        // a UI usa para tocar e arrastar o objeto com o dedo) seguem ela.
        if (persp && sceneEditor_.enabled)
            m = scene_editor_projection(comp->width(), comp->height(), sceneEditor_) * layer_world_3d(*comp, *l, now);
        const f32 w = static_cast<f32>(out.sourceWidth), h = static_cast<f32>(out.sourceHeight);
        // Vetor: a caixa do conteúdo pode começar fora da origem da camada.
        Vec2 o{0.0f, 0.0f};
        if (l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector) {
            const Rect b = vector_content_box(*l, l->local_time(now));
            o = Vec2{b.x, b.y};
        }
        const f32 xs[4] = {o.x, o.x + w, o.x + w, o.x}, ys[4] = {o.y, o.y, o.y + h, o.y + h};
        bool ok = true;
        for (int i = 0; i < 4; ++i) {
            const Vec4 v = m * Vec4{xs[i], ys[i], 0, 1};
            if (!(v.w > 1e-6f)) { ok = false; break; }   // canto atrás da câmera
            out.corners[i * 2] = v.x / v.w;
            out.corners[i * 2 + 1] = v.y / v.w;
        }
        if (ok) out.geomFlags = bridge::kGeomCornersValid | (persp ? bridge::kGeomPerspective : 0u);
    }
    return true;
}

bool Engine::fill_layer_detail_locked(u64 layerId, bridge::LayerDetailPOD& out) noexcept {
    Composition* comp = current_composition();
    if (!comp || !project_) return false;
    const Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return false;
    const FrameIndex local = l->local_time(playback_.current());
    auto value = [&](TrackProperty p, f32 fallback, u32 bit) noexcept {
        const Track* tr = l->tracks.find(p);
        if (!tr) return fallback;
        if (!tr->keys.empty()) {
            out.animatedMask |= 1u << bit;
            if (tr->find_exact(local) != kInvalidIndex) out.keyAtPlayheadMask |= 1u << bit;
        }
        // Com expressão, a UI mostra o valor RESULTANTE (como o After Effects).
        return tr->value_or(local, fallback);
    };
    using TP = TrackProperty;
    const Transform& tf = l->transform;
    out.id = layerId;
    out.kind = static_cast<u32>(l->kind);
    bool selected = false;
    for (u64 s : selection_) if (s == layerId) selected = true;
    out.flags = (l->visible ? bridge::kLayerRowFlagVisible : 0u) | (l->locked ? bridge::kLayerRowFlagLocked : 0u)
              | (selected ? bridge::kLayerRowFlagSelected : 0u) | (l->solo ? bridge::kLayerRowFlagSolo : 0u)
              | (l->adjustment ? bridge::kLayerRowFlagAdjustment : 0u) | (l->guide ? bridge::kLayerRowFlagGuide : 0u)
              | (l->threeD ? bridge::kLayerRowFlagThreeD : 0u)
              | (l->captions.empty() ? 0u : bridge::kLayerRowFlagCaptions)
              | (l->magneticTrack ? bridge::kLayerRowFlagMagnetic : 0u)
              | ((static_cast<u32>(l->label) << bridge::kLayerRowLabelShift) & bridge::kLayerRowLabelMask);
    out.startFrame = static_cast<i32>(l->start.value);
    out.endFrame = static_cast<i32>(l->end.value);
    out.offsetFrames = static_cast<i32>(l->offset.value);
    out.blendMode = static_cast<u32>(l->blendMode);
    out.position[0] = value(TP::PositionX, tf.position.x, 0);
    out.position[1] = value(TP::PositionY, tf.position.y, 1);
    out.position[2] = value(TP::PositionZ, tf.position.z, 2);
    out.scale[0] = value(TP::ScaleX, tf.scale.x, 3);
    out.scale[1] = value(TP::ScaleY, tf.scale.y, 4);
    out.scale[2] = value(TP::ScaleZ, tf.scale.z, 5);
    out.rotation[0] = value(TP::RotationX, tf.rotation.x, 6);
    out.rotation[1] = value(TP::RotationY, tf.rotation.y, 7);
    out.rotation[2] = value(TP::RotationZ, tf.rotation.z, 8);
    out.anchor[0] = value(TP::AnchorX, tf.anchor.x, 9);
    out.anchor[1] = value(TP::AnchorY, tf.anchor.y, 10);
    out.anchor[2] = value(TP::AnchorZ, tf.anchor.z, 11);
    out.opacity = value(TP::Opacity, tf.opacity, 12);
    out.skew[0] = value(TP::SkewX, tf.skewX, 13);
    out.skew[1] = value(TP::SkewY, tf.skewY, 14);
    out.effectCount = static_cast<u32>(l->effects.size());
    out.maskCount = static_cast<u32>(l->masks.size());
    out.localPlayhead = static_cast<i32>(local.value);
    out.parentId = l->parent.valid() ? l->parent.pack() : 0;
    out.audioGain = l->gain;
    out.audioVolume = l->tracks.sample_or(TrackProperty::AudioVolume, local, 1.0f);
    out.audioPan = l->pan;
    out.audioFadeIn = static_cast<i32>(l->fadeIn.value);
    out.audioFadeOut = static_cast<i32>(l->fadeOut.value);
    out.speed = l->speed_at(playback_.current());
    out.timeFlags = (l->speed_track() ? 64u : 0u) | (l->reversed ? 1u : 0u) | (l->motionBlur ? 2u : 0u) | (l->timeRemapEnabled ? 4u : 0u)
                  | (l->frameBlend == 1 ? 8u : 0u) | (l->frameBlend == 2 ? 16u : 0u) | (l->vectorBlur > 0.0f ? 32u : 0u)
                  | (l->keepPitch ? 128u : 0u)
                  | (l->timeRemapEnabled && l->timeRemap.keys.size() >= 2
                     && l->timeRemap.keys.back().value < l->timeRemap.keys.front().value ? 256u : 0u);
    // Transições: tipo entrada (4 bits) | saída (4) | quadros entrada (12) | saída (12).
    out.reserved0 = (l->transitionIn & 0xFu) | ((l->transitionOut & 0xFu) << 4)
                  | ((std::min<u32>(l->transitionInFrames, 4095u)) << 8) | ((std::min<u32>(l->transitionOutFrames, 4095u)) << 20);
    {
        const Asset* aa = project_->asset(l->source);
        const Track* vt = l->tracks.find(TrackProperty::AudioVolume);
        out.audioFlags = (l->muted ? bridge::kAudioFlagMuted : 0u) | (l->solo ? bridge::kAudioFlagSolo : 0u)
                       | ((aa && aa->has_audio() && (l->kind == LayerKind::Video || l->kind == LayerKind::Audio))
                              ? bridge::kAudioFlagHasAudio : 0u)
                       | ((vt && vt->animated()) ? bridge::kAudioFlagVolumeAnimated : 0u);
    }
    if (l->kind == LayerKind::Null) {
        out.sourceWidth = 100;
        out.sourceHeight = 100;
        return true;
    }
    if (l->kind == LayerKind::ParticleSystem) {
        out.sourceWidth = comp->width();
        out.sourceHeight = comp->height();
        return true;
    }
    if (l->kind == LayerKind::Composition) {
        if (const Composition* child = project_->timeline().composition(l->nested.composition)) {
            out.sourceWidth = child->width();
            out.sourceHeight = child->height();
            out.sourceFps = static_cast<f32>(child->fps());
            out.sourceFrames = static_cast<i32>(child->duration().value);
        }
        return true;
    }
    if (l->kind == LayerKind::Text) {
        if (const auto font = text::FontManager::instance().font_for(l->text)) {
            const text::TextExtent ext = text::measure(*font, l->text);
            const f32 pad = l->text.strokeWidth > 0.0f ? l->text.strokeWidth + 2.0f : 2.0f;
            out.sourceWidth = static_cast<u32>(std::ceil(ext.width + 2.0f * pad));
            out.sourceHeight = static_cast<u32>(std::ceil(ext.height + 2.0f * pad));
        }
        return true;
    }
    if (l->kind == LayerKind::Shape) {
        auto rgba8 = [](Vec4 c) {
            auto b = [](f32 v) { return static_cast<u32>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16) | (b(c.w) << 24);
        };
        const ShapeData& sh = l->shape;
        out.sourceWidth = static_cast<u32>(std::max(1.0f, sh.bounds.w));
        out.sourceHeight = static_cast<u32>(std::max(1.0f, sh.bounds.h));
        if (sh.shapeType == kShapeVector) {
            const Rect b = vector_content_box(*l, local);
            out.sourceWidth = static_cast<u32>(std::max(1.0f, std::ceil(b.w)));
            out.sourceHeight = static_cast<u32>(std::max(1.0f, std::ceil(b.h)));
        }
        out.shapeTypePoints = sh.shapeType | (static_cast<u32>(sh.points) << 16);
        out.shapeFill = sh.filled ? rgba8(sh.fillColor) : (rgba8(sh.fillColor) & 0x00FFFFFFu);
        out.shapeStroke = rgba8(sh.strokeColor);
        out.shapeStrokeWidth = sh.strokeWidth;
        out.shapeCorner = sh.cornerRadius;
        out.shapeInner = sh.innerRadius;
        return true;
    }
    if (l->kind == LayerKind::Model3D) {
        // Silhueta de frente em escala 100% (o plano Z=0 é 1:1 com a
        // composição): é a caixa que o palco desenha e toca. A UI trata a
        // âncora da layer 3D como o CENTRO dessa caixa (o pivô do modelo).
        if (const auto it = models_.find(l->model.scene.pack()); it != models_.end()) {
            const Vec3 ext = it->second->bounds.extent();
            out.sourceWidth = static_cast<u32>(std::max(1.0f, std::round(ext.x * l->model.unitScale)));
            out.sourceHeight = static_cast<u32>(std::max(1.0f, std::round(ext.y * l->model.unitScale)));
        }
        return true;
    }
    if (l->source.valid()) {
        if (const Asset* a = project_->asset(l->source)) {
            out.sourceWidth = a->video.width;
            out.sourceHeight = a->video.height;
            out.sourceFps = static_cast<f32>(a->video.fps);
            if (a->kind == AssetKind::Video && a->video.fps > 0.0) {
                out.sourceFrames = static_cast<i32>(std::llround(static_cast<f64>(a->video.frameCount.value)
                                                                 * comp->fps() / a->video.fps));
            }
        }
    }
    return true;
}

bool Engine::query_keyframe_easing(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex,
                                   i32 frame, f32* out4, u8* outPower) noexcept {
    if (!out4 || property >= static_cast<u32>(TrackProperty::_Count)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = current_composition();
    const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const TrackRef ref{LayerId::unpack(layerId), static_cast<TrackProperty>(property), effectIndex, paramIndex};
    const Track* track = layer ? read_command_track(*layer, ref) : nullptr;
    if (!track) return false;
    const u32 index = track->find_exact(FrameIndex{frame});
    if (index == kInvalidIndex) return false;
    const auto& key = track->keys[index];
    out4[0] = key.bx1; out4[1] = key.by1; out4[2] = key.bx2; out4[3] = key.by2;
    if (outPower) *outPower = clamp_ease_power(key.easePower);
    return true;
}

u32 Engine::query_curve(u64 layerId, u32 property, i32 startFrame, i32 endFrame,
                        f32* outValues, u32 sampleCount) noexcept {
    return query_track_curve(layerId, property, kInvalidIndex, 0, startFrame, endFrame, outValues, sampleCount);
}

u32 Engine::query_track_curve(u64 layerId, u32 property, u32 effectIndex, u32 paramIndex,
                              i32 startFrame, i32 endFrame, f32* outValues, u32 sampleCount) noexcept {
    if (!outValues || sampleCount == 0 || endFrame <= startFrame || property >= static_cast<u32>(TrackProperty::_Count)) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = current_composition();
    if (!comp) return 0;
    Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return 0;
    const TrackRef ref{LayerId::unpack(layerId), static_cast<TrackProperty>(property), effectIndex, paramIndex};
    const Track* track = read_command_track(*l, ref);
    if (!track) return 0;
    const f64 step = (static_cast<f64>(endFrame) - startFrame) / static_cast<f64>(sampleCount > 1 ? sampleCount - 1 : 1);
    for (u32 i = 0; i < sampleCount; ++i) {
        outValues[i] = track->sample_keys(FrameIndex{static_cast<i64>(static_cast<f64>(startFrame) + step * i)});
        if (is_remap_time_alias(*l, ref)) outValues[i] /= static_cast<f32>(comp->fps());
    }
    return sampleCount;
}

u32 Engine::query_effect_catalog(bridge::EffectCatalogRow* out, u32 capacity, char* blob,
                                 u32 blobCapacity) noexcept {
    if (!out) return 0;
    // Aposentados do CATÁLOGO (não do registro): projetos antigos abrem e tocam
    // igual, mas quem adiciona um efeito recebe as versões completas do pacote
    // de áudio — os envios de reverb/flanger/eco sem estado e o espectro antigo.
    static const EffectTypeId kRetired[] = {
        effect_type_id("aurea.audio.reverb"), effect_type_id("aurea.audio.flanger"),
        effect_type_id("aurea.audio.echo"), effect_type_id(effect_keys::kAudioSpectrum),
        // Created together with their controller/membership by create_grid.
        effect_type_id("aurea.layout.grid_builder"), effect_type_id("aurea.layout.grid_item"),
    };
    u32 cursor = 0, written = 0;
    for (u32 i = 0; i < effectRegistry_.count() && written < capacity; ++i) {
        const Effect& e = effectRegistry_.at(i);
        if (std::find(std::begin(kRetired), std::end(kRetired), e.type_id()) != std::end(kRetired)) continue;
        bridge::EffectCatalogRow row;
        row.typeId = e.type_id();
        row.effectClass = static_cast<u32>(e.effect_class());
        row.paramCount = effectRegistry_.params_at(i).count();
        (void)put_string(blob, blobCapacity, cursor, e.info().name, row.nameOffset, row.nameLength);
        (void)put_string(blob, blobCapacity, cursor, e.info().category, row.categoryOffset, row.categoryLength);
        out[written++] = row;
    }
    return written;
}

u32 Engine::query_layer_effects(u64 layerId, bridge::LayerEffectRow* out, u32 capacity, char* blob,
                                u32 blobCapacity) noexcept {
    if (!out) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    // O efeito que o painel acabou de pôr (ainda na fila) já aparece na pilha.
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return 0;
    const Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return 0;
    u32 cursor = 0, written = 0;
    for (const EffectInstance& inst : l->effects) {
        if (written >= capacity) break;
        const Effect* e = effectRegistry_.find(inst.type);
        bridge::LayerEffectRow row;
        row.effectId = inst.id;
        row.typeId = inst.type;
        row.enabled = inst.enabled ? 1u : 0u;
        row.paramCount = static_cast<u32>(inst.params.size());
        row.known = e ? 1u : 0u;
        (void)put_string(blob, blobCapacity, cursor, e ? e->info().name : "efeito indisponivel",
                         row.nameOffset, row.nameLength);
        out[written++] = row;
    }
    return written;
}

u32 Engine::query_effect_params(u64 layerId, u32 effectId, bridge::EffectParamRow* out, u32 capacity,
                                char* blob, u32 blobCapacity) noexcept {
    if (!out) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    // Escritas ainda na fila (o painel acabou de mexer num valor) entram
    // ANTES da leitura: sem isto o painel relia o valor antigo logo depois de
    // escrever, o controle voltava e um vetor (cor, ponto) escrito por
    // componente perdia o que já tinha mudado.
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return 0;
    Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return 0;
    EffectInstance* inst = l->find_effect(EffectId{effectId, 0});
    if (!inst) return 0;
    const ParameterRegistry* params = effectRegistry_.params(inst->type);
    if (!params) return 0;

    const FrameIndex local = l->local_time(playback_.current());
    u32 cursor = 0, written = 0;
    for (u32 i = 0; i < params->count() && written < capacity; ++i) {
        // Extruded glyphs support geometry, opacity and fill. Raster-only
        // blur/stroke controls belong to 2D text and its 3D planes.
        if (inst->type == effect_type_id(text::kAnimatorEffect) && l->kind == LayerKind::Model3D &&
            (i == text::aeBlur || i == text::aeStrokeWidth || i == text::aeStrokeOn || i == text::aeStroke)) continue;
        const ParamSpec& spec = params->at(i);
        bridge::EffectParamRow row;
        row.index = i;
        row.type = static_cast<u32>(spec.type);
        row.flags = spec.flags;
        row.enumCount = spec.enumCount;
        row.minValue = spec.minValue;
        row.maxValue = spec.maxValue;
        row.hardMin = spec.typed_min();
        row.hardMax = spec.typed_max();
        const ParamValue v = evaluate_param(l->tracks, *inst, i, spec, local);
        for (int c = 0; c < 4; ++c) {
            row.value[c] = v.v[c];
            row.defaultValue[c] = spec.defaultValue.v[c];
        }
        // Referência a camada: o valor é o ÍNDICE da camada escolhida (−1 =
        // nenhuma, ou a que sumiu). É o mesmo número que a UI escreve.
        if (spec.type == ParamType::LayerReference) {
            const u64 ref = i < inst->params.size() ? inst->params[i].constant.ref : 0;
            row.value[0] = ref && comp->layer(LayerId::unpack(ref)) ? static_cast<f32>(LayerId::unpack(ref).index) : -1.0f;
            row.defaultValue[0] = -1.0f;
        }
        (void)put_string(blob, blobCapacity, cursor, spec.label, row.labelOffset, row.labelLength);
        (void)put_string(blob, blobCapacity, cursor, spec.unit, row.unitOffset, row.unitLength);
        if (spec.enumCount && spec.enumLabels) {
            row.enumOffset = cursor;
            for (u32 k = 0; k < spec.enumCount; ++k) {
                u32 o = 0, n = 0;
                if (k) (void)put_string(blob, blobCapacity, cursor, "|", o, n);
                (void)put_string(blob, blobCapacity, cursor, spec.enumLabels[k], o, n);
            }
            row.enumLength = cursor - row.enumOffset;
        }
        // O id por último: se o blob encher, perde-se só a chave de tradução
        // (a UI cai no rótulo do motor), nunca o rótulo.
        (void)put_string(blob, blobCapacity, cursor, spec.id, row.idOffset, row.idLength);
        const u32 comps = component_count(spec.type);
        for (u32 c = 0; c < comps; ++c) {
            const Track* t = l->tracks.find(TrackProperty::EffectParam, inst->id, param_track_key(i, c));
            if (t && !t->keys.empty()) row.animated = 1;
        }
        // Remapear tempo: "Tempo" e "Interpolação do tempo" leem a CURVA da
        // camada, não um parâmetro guardado — é o mesmo dado do gráfico.
        if (is_time_remap_type(inst->type)) {
            const f64 compFps = comp->fps() > 0.0 ? comp->fps() : 30.0;
            if (i == 0) {
                const f32 base = static_cast<f32>(l->source_frame(playback_.current()));
                const f32 frames = l->timeRemapEnabled ? l->timeRemap.value_or(local, base) : base;
                row.value[0] = static_cast<f32>(frames / compFps);   // segundos da fonte
                row.animated = l->timeRemap.keys.size() > 1 ? 1u : 0u;
            } else if (i == 1) {
                const u32 at = l->timeRemap.find_exact(local);
                row.value[0] = at == kInvalidIndex ? 0.0f : remap_mode_of_interp(l->timeRemap.keys[at].interp);
            }
        }
        out[written++] = row;
    }
    return written;
}

u32 Engine::query_effect_specs(u32 typeId, bridge::EffectParamRow* out, u32 capacity, char* blob,
                               u32 blobCapacity) noexcept {
    if (!out) return 0;
    const ParameterRegistry* params = effectRegistry_.params(typeId);
    if (!params) return 0;
    u32 cursor = 0, written = 0;
    for (u32 i = 0; i < params->count() && written < capacity; ++i) {
        const ParamSpec& spec = params->at(i);
        bridge::EffectParamRow row;
        row.index = i;
        row.type = static_cast<u32>(spec.type);
        row.flags = spec.flags;
        row.enumCount = spec.enumCount;
        row.minValue = spec.minValue;
        row.maxValue = spec.maxValue;
        row.hardMin = spec.typed_min();
        row.hardMax = spec.typed_max();
        // Sem instância não há valor corrente: o padrão da declaração responde
        // pelos dois, e nada aparece animado.
        for (int c = 0; c < 4; ++c) {
            row.value[c] = spec.defaultValue.v[c];
            row.defaultValue[c] = spec.defaultValue.v[c];
        }
        (void)put_string(blob, blobCapacity, cursor, spec.label, row.labelOffset, row.labelLength);
        (void)put_string(blob, blobCapacity, cursor, spec.unit, row.unitOffset, row.unitLength);
        if (spec.enumCount && spec.enumLabels) {
            row.enumOffset = cursor;
            for (u32 k = 0; k < spec.enumCount; ++k) {
                u32 o = 0, n = 0;
                if (k) (void)put_string(blob, blobCapacity, cursor, "|", o, n);
                (void)put_string(blob, blobCapacity, cursor, spec.enumLabels[k], o, n);
            }
            row.enumLength = cursor - row.enumOffset;
        }
        (void)put_string(blob, blobCapacity, cursor, spec.id, row.idOffset, row.idLength);
        out[written++] = row;
    }
    return written;
}

// =============================================================================
// Seleção
// =============================================================================
void Engine::set_selection(const u64* layerIds, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    selection_.clear();
    if (!layerIds) return;
    for (u32 i = 0; i < count; ++i) if (layerIds[i] != 0) selection_.push_back(layerIds[i]);
    std::sort(selection_.begin(), selection_.end());
    selection_.erase(std::unique(selection_.begin(), selection_.end()), selection_.end());
}

void Engine::clear_selection() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    selection_.clear();
}

u32 Engine::selection_count() const noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return static_cast<u32>(selection_.size());
}

u32 Engine::get_selection(u64* out, u32 capacity) const noexcept {
    if (!out) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    const u32 n = std::min<u32>(static_cast<u32>(selection_.size()), capacity);
    for (u32 i = 0; i < n; ++i) out[i] = selection_[i];
    return n;
}

bool Engine::is_selected(u64 layerId) const noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return std::binary_search(selection_.begin(), selection_.end(), layerId);
}

// =============================================================================
// Export
// =============================================================================
i64 Engine::query_export_duration(bool trimToContent) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    const Composition* comp = current_composition();
    return comp ? comp->export_duration(trimToContent).value : 0;
}

Status Engine::start_export(const ExportSettings& settings, const char* outputPath) noexcept {
    if (!project_) return Errc::InvalidState;
    if (settings.aiUpscale != 0 && settings.aiUpscale != 2 && settings.aiUpscale != 4)
        return Status{Errc::InvalidArgument, "escala neural deve ser 2x ou 4x"};
    if (!outputPath || !*outputPath) return Errc::InvalidArgument;
    if (!gpu_ || !renderer_.ready()) return Status{Errc::NotSupported, "export precisa de GPU"};
    if (!config_.exportSinkFactory) return Status{Errc::NotSupported, "sem encoder de video nesta plataforma"};
    if (exportCtx_ && exportCtx_->thread.joinable()) {
        bool running = false;
        {
            std::lock_guard<std::mutex> pl(exportCtx_->mutex);
            running = exportCtx_->progress.running;
        }
        if (running) return Status{Errc::InvalidState, "ja existe um export em andamento"};
        exportCtx_->thread.join();
    }

    auto ctx = std::make_unique<ExportContext>();
    ctx->settings = settings;
    ctx->outputPath = outputPath;
    ctx->dither = settings.dither;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        Composition* comp = current_composition();
        if (!comp) return Errc::NotFound;
        ctx->compFps = comp->fps();
        ctx->fps = settings.fps > 0.0 ? settings.fps : comp->fps();
        // Lado menor pedido; a largura vem da proporção da composição (o
        // renderer preenche o alvo inteiro — outra proporção esticaria).
        const u32 cw = comp->width(), ch = comp->height();
        const u32 compShort = std::min(cw, ch);
        const u32 wantShort = settings.height > 0 ? settings.height : compShort;
        const f64 k = static_cast<f64>(wantShort) / static_cast<f64>(compShort);
        auto even = [](f64 v) { return std::max<u32>(2u, static_cast<u32>(std::llround(v / 2.0)) * 2u); };
        ctx->width = even(cw * k);
        ctx->height = even(ch * k);
        const f64 seconds = static_cast<f64>(comp->export_duration(settings.trimToContent).value) / comp->fps();
        ctx->frames = std::max<u32>(1u, static_cast<u32>(std::ceil(seconds * ctx->fps - 1e-6)));
        ctx->audioCache = std::make_unique<audio::AudioBlockCache>(config_.mediaFactory, 16ull << 20, false);
        ctx->audioSnap = audio::build_snapshot(*comp, *project_, ctx->audioCache.get(), &Engine::audio_path_resolver, this);
    }
    // Teto do aparelho (a mesma regra lado maior × lado menor da composição).
    const u32 capLong = std::max(caps_.max_export_width(), caps_.max_export_height());
    const u32 capShort = std::min(caps_.max_export_width(), caps_.max_export_height());
    if (capLong > 0 && (std::max(ctx->width, ctx->height) > capLong || std::min(ctx->width, ctx->height) > capShort)) {
        return Status{Errc::NotSupported, "resolucao acima do que este aparelho exporta"};
    }
    const u32 maxTex = gpu_->capabilities().maxTexture2D;
    if (maxTex > 0 && std::max(ctx->width, ctx->height) > maxTex) {
        return Status{Errc::NotSupported, "resolucao acima do limite de textura da GPU"};
    }
    ctx->outputWidth = ctx->width;
    ctx->outputHeight = ctx->height;
    if (settings.aiUpscale) {
        // Keep the entire composition and the selected output exactly. Round the
        // inference input to even dimensions, then shrink the full neural image.
        const u32 scale = settings.aiUpscale;
        ctx->width = ((ctx->outputWidth + scale * 2 - 1) / (scale * 2)) * 2;
        ctx->height = ((ctx->outputHeight + scale * 2 - 1) / (scale * 2)) * 2;
        const u64 working = static_cast<u64>(ctx->width) * ctx->height * 3 +
                            static_cast<u64>(ctx->width * scale) * (ctx->height * scale) * 3 / 2 + (64ull << 20);
        if (working > std::max<u64>(32ull << 20, caps_.memory_budget_bytes() / 2))
            return Status{Errc::OutOfMemory, "memoria insuficiente para upscale nesta resolucao"};
        ctx->dither = false; // Do not feed quantization noise to the neural network.
    }

    struct ProxyExportPause {
        PreviewProxyService& service;
        bool started = false;
        ~ProxyExportPause() { if (!started) service.set_pause_reason(PreviewProxyService::Export, false); }
    } proxyPause{media_.proxies()};
    media_.proxies().set_pause_reason(PreviewProxyService::Export, true);
    ctx->sink = config_.exportSinkFactory(config_.exportSinkContext);
    if (!ctx->sink) return Status{Errc::NotSupported, "encoder indisponivel"};

    VideoStreamConfig vc;
    vc.width = ctx->outputWidth;
    vc.height = ctx->outputHeight;
    vc.fps = ctx->fps;
    vc.codec = settings.videoCodec;
    // A taxa vem da MESMA regra que a tela Exportar mostra (BitratePolicy):
    // 1080p30 Normal ≈ 14 Mbps, 4K30 ≈ 40 Mbps; Mbps manual só com teto.
    vc.bitrateBps = export_video_bitrate_bps(ctx->outputWidth, ctx->outputHeight, ctx->fps, settings.videoCodec,
                                             static_cast<ExportQuality>(std::min<u32>(settings.quality, 2u)),
                                             settings.videoBitrateMbps);
    // CBR só quando pedido; o padrão é VBR com pico limitado no encoder.
    vc.rateMode = settings.rateMode == 0 ? 0u : 1u;
    vc.keyframeIntervalFrames = settings.keyframeIntervalFrames;
    // Com som na timeline, o arquivo leva AAC; sem, só vídeo (uma trilha de
    // silêncio não serve para nada e alguns players a mostram como "com som").
    AudioStreamConfig ac;
    ac.sampleRate = audio::kMixRate;
    ac.channels = audio::kMixChannels;
    ac.bitrateBps = std::clamp<u32>(settings.audioBitrateKbps, 64, 320) * 1000u;
    const bool withAudio = ctx->audioSnap && ctx->audioSnap->audible();
    if (!withAudio) ctx->audioSnap.reset();
    // Espaço em disco ANTES de abrir encoder e GPU: um export de horas que
    // enche o aparelho no meio perde o tempo todo. A estimativa é o bitrate
    // PEDIDO (o encoder real costuma ficar abaixo, sobretudo em resolução
    // baixa), então só recusa quando nem metade cabe; o resto o sink pega
    // (ENOSPC → StorageFull) e o encoder confere a cada 300 quadros.
    {
        const f64 seconds = static_cast<f64>(ctx->frames) / ctx->fps;
        const f64 bps = static_cast<f64>(vc.bitrateBps) + (withAudio ? static_cast<f64>(ac.bitrateBps) : 0.0);
        const u64 estimate = static_cast<u64>(seconds * bps / 8.0 * 1.05) + (8ull << 20);
        std::error_code ec;
        std::filesystem::path dir = std::filesystem::u8path(outputPath).parent_path();
        if (dir.empty()) dir = std::filesystem::current_path(ec);
        const std::filesystem::space_info sp = std::filesystem::space(dir, ec);
        ctx->estimatedBytes = estimate;
        if (!ec && sp.available != static_cast<std::uintmax_t>(-1) && sp.available < estimate / 2 + (32ull << 20)) {
            static thread_local char detail[160];   // Status guarda o ponteiro
            std::snprintf(detail, sizeof(detail), "espaco livre insuficiente: o video precisa de ~%llu MB e ha %llu MB livres",
                          static_cast<unsigned long long>(estimate >> 20),
                          static_cast<unsigned long long>(sp.available >> 20));
            AUREA_LOG_WARN("export: %s", detail);
            return Status{Errc::StorageFull, detail};
        }
        ctx->outputDir = dir;
    }
    if (const Status s = ctx->sink->open(outputPath, vc, withAudio ? &ac : nullptr); !s.ok()) return s;

    // O encoder que o sink REALMENTE abriu. Quando o sink não sabe dizer (host,
    // plataforma sem essa consulta), vale a tabela do MediaCodecList.
    {
        const ExportSink::EncoderInfo enc = ctx->sink->encoder_info();
        const CodecCapability& table = vc.codec == ExportCodec::HEVC ? caps_.encoder_hevc() : caps_.encoder_h264();
        bool hw = false, sw = false;
        if (enc.acceleration == ExportSink::Acceleration::Hardware) hw = true;
        else if (enc.acceleration == ExportSink::Acceleration::Software) sw = true;
        else if (table.supported) (table.hardwareAccelerated ? hw : sw) = true;
        if (hw) ctx->progress.flags |= kExportHardwareEncoder;
        if (sw) {
            ctx->progress.flags |= kExportSoftwareEncoder;
            AUREA_LOG_WARN("export: encoder de SOFTWARE (%s) — mais lento; a qualidade pedida nao muda",
                           enc.name[0] ? enc.name : "tabela do aparelho");
        } else {
            AUREA_LOG_INFO("export: encoder %s (%s)", enc.name[0] ? enc.name : "?",
                           hw ? "hardware" : "aceleracao desconhecida");
        }
    }

    // Alvos de GPU da sessão: composição (linear), Y e CbCr.
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        TextureDesc cd;
        cd.width = ctx->width;
        cd.height = ctx->height;
        cd.format = SurfaceFormat::RGBA16F;
        cd.sampled = true;
        cd.renderTarget = true;
        cd.transferSrc = true;
        cd.debugName = "export-composicao";
        TextureDesc yd = cd;
        yd.format = SurfaceFormat::R8;
        yd.debugName = "export-y";
        TextureDesc ud = yd;
        ud.width = ctx->width / 2;
        ud.height = ctx->height / 2;
        ud.format = SurfaceFormat::RG8;
        ud.debugName = "export-cbcr";
        auto c = gpu_->create_texture(cd);
        auto y = gpu_->create_texture(yd);
        auto u = gpu_->create_texture(ud);
        if (!c.ok() || !y.ok() || !u.ok()) {
            if (c.ok()) gpu_->destroy_texture(*c);
            if (y.ok()) gpu_->destroy_texture(*y);
            if (u.ok()) gpu_->destroy_texture(*u);
            ctx->sink->abort();
            return Status{Errc::OutOfMemory, "sem memoria de GPU para o export"};
        }
        ctx->comp = *c;
        ctx->y = *y;
        ctx->uv = *u;

        // Slots de leitura (host-visible, mapeados de vez). Sem memória para os
        // três, o export segue com menos — mais lento, nunca pior.
        // Sem valor fixo na config, vale a política do APARELHO (DevicePolicy:
        // 2 no perfil de entrada, 4 no topo, menos com calor). Antes o motor
        // ignorava a tabela e usava 3 em todo celular: um aparelho de 4 GB
        // segurava um slot de leitura a mais do que a política dele manda.
        const u32 policyDepth = caps_.policy().exportPipelineDepth;
        const u32 want = std::clamp<u32>(config_.exportPipelineDepth ? config_.exportPipelineDepth
                                                                     : (policyDepth ? policyDepth : 3u), 1u, 4u);
        for (u32 k = 0; k < want; ++k) {
            BufferDesc bd;
            bd.usage = BufferUsage::TransferDst;
            bd.access = MemoryAccess::Readback;
            bd.bytes = static_cast<usize>(ctx->width) * ctx->height;
            bd.debugName = "export-leitura-y";
            auto by = gpu_->create_buffer(bd);
            bd.bytes = static_cast<usize>(ctx->width) * (ctx->height / 2);
            bd.debugName = "export-leitura-cbcr";
            auto bu = gpu_->create_buffer(bd);
            void* py = nullptr;
            void* pu = nullptr;
            if (!by.ok() || !bu.ok() || !gpu_->map_buffer(*by, py).ok() || !gpu_->map_buffer(*bu, pu).ok()) {
                if (by.ok()) gpu_->destroy_buffer(*by);
                if (bu.ok()) gpu_->destroy_buffer(*bu);
                break;
            }
            ExportContext::Slot slot;
            slot.y = *by;
            slot.uv = *bu;
            slot.yPtr = static_cast<const u8*>(py);
            slot.uvPtr = static_cast<const u8*>(pu);
            ctx->slots.push_back(slot);
        }
        if (ctx->slots.empty()) {
            gpu_->destroy_texture(ctx->comp);
            gpu_->destroy_texture(ctx->y);
            gpu_->destroy_texture(ctx->uv);
            ctx->sink->abort();
            return Status{Errc::OutOfMemory, "sem memoria para ler os quadros do export"};
        }
        ctx->depth = static_cast<u32>(ctx->slots.size());
        for (u32 k = 0; k < ctx->depth; ++k) ctx->freeSlots.push_back(ctx->depth - 1 - k);
    }

    ctx->progress.running = true;
    ctx->progress.framesTotal = ctx->frames;
    ctx->progress.pipelineDepth = ctx->depth;
    ctx->set_message("exportando");
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        playback_.pause(monotonic_ns());
        playingHint_ = false;
        audio_.stop();
    }
    exportCtx_ = std::move(ctx);
    exportActive_.store(true, std::memory_order_release);
    media_.proxies().set_pause_reason(PreviewProxyService::Playback, false);
    proxyPause.started = true;
    exportCtx_->thread = std::thread([this] { export_thread_main(); });
    return OkStatus;
}

Status Engine::wait_export_gpu(u64 gpuFrame) noexcept {
    if (!gpu_) return Status{Errc::InvalidState, "sem GPU"};
    const u64 deadline = monotonic_ns() + 120'000'000'000ull;
    for (;;) {
        if (exportCtx_->cancelRequested.load(std::memory_order_acquire)) return Errc::Cancelled;
        const Status s = gpu_->wait_frame(gpuFrame, 100'000'000ull);
        if (s.ok() || s.code() != Errc::Timeout || monotonic_ns() >= deadline) return s;
    }
}

Status Engine::render_export_frame(FrameIndex t, const OffscreenTarget& target, u64& gpuFrame) noexcept {
    ExportContext& c = *exportCtx_;
    gpuFrame = 0;
    RenderSettings rs;
    rs.dither = false;
    rs.gpuTimers = config_.enableTelemetry;
    rs.finalQuality = true;
    // Quadros EXATOS de vídeo, em sequência (o modo Playback decodifica
    // adiante). Falta quadro: espera o decoder AVISAR que entregou (antes era
    // um sono cego de 5 ms por tentativa), no máximo 4 s — arquivo quebrado
    // não trava. O lock do render só vale para preparar e gravar: esperando o
    // decoder, a GPU fica livre para quem precisar (captura, prévia de efeito).
    const u64 t0 = monotonic_ns();
    // O prazo de 4 s conta SEM PROGRESSO do decoder: efeito que lê outros
    // instantes (RGB no tempo, mistura de quadros) pode precisar de vários
    // quadros por quadro de saída — e com GOP longo isso passa de 4 s sem que
    // nada esteja quebrado. Teto absoluto de 60 s por quadro.
    //
    // Esgotado o prazo, o export NÃO aborta: grava com o quadro decodificado
    // mais próximo (uma camada sem quadro nenhum fica de fora deste quadro),
    // registra e segue. Um vídeo de horas não pode morrer por um quadro que o
    // decoder do aparelho não entregou. Depois de um fallback a paciência cai
    // para 1 s até um quadro exato voltar (fonte quebrada ≠ 4 s por quadro).
    const u64 patience = c.sourceDegraded ? 1'000'000'000ull : 4'000'000'000ull;
    u64 deadline = t0 + patience;
    u64 hardDeadline = t0 + 60'000'000'000ull;
    u64 lastGen = mediaReadyGen_.load(std::memory_order_acquire);
    bool drainedGpu = false;
    std::unique_lock<std::mutex> rl(renderMutex_, std::defer_lock);
    auto prepare = [&]() -> Status {
        if (!gpu_ || !renderer_.ready()) return Status{Errc::InvalidState, "sem GPU"};
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Errc::InvalidState;
        Composition* comp = current_composition();
        if (!comp) return Errc::NotFound;
        renderer_.prepare(*comp, *project_, t, &media_, &Engine::image_lookup, this, rs, ++frameCounter_,
                          1, DecodeMode::Playback, 1.0f, snapshot_);
        return OkStatus;
    };
    for (;;) {
        const u64 gen = mediaReadyGen_.load(std::memory_order_acquire);
        rl.lock();
        if (const Status s = prepare(); !s.ok()) return s;
        if (snapshot_.missingVideoFrames == 0 && snapshot_.staleVideoFrames == 0) { c.sourceDegraded = false; break; }
        const u64 now = monotonic_ns();
        if (gen != lastGen) { lastGen = gen; deadline = std::min<u64>(now + patience, hardDeadline); }
        if (now > deadline) {
            const u32 missing = snapshot_.missingVideoFrames, stale = snapshot_.staleVideoFrames;
            c.sourceDegraded = true;
            if (c.fallbackFrames++ < 8 || c.fallbackFrames % 300 == 0) {
                AUREA_LOG_WARN("export: quadro %lld sem o video exato apos %.1f s (%u sem quadro, %u aproximados); "
                               "gravando o mais proximo decodificado (%llu ate agora)",
                               static_cast<long long>(t.value), static_cast<f64>(now - t0) / 1e9, missing, stale,
                               static_cast<unsigned long long>(c.fallbackFrames));
            }
            if (c.fallbackFrames == 1) {
                std::lock_guard<std::mutex> pl(c.mutex);
                c.progress.flags |= kExportFrameFallback;
            }
            break;   // o snapshot já preparado vai para a GPU como está
        }
        snapshot_.release_video_frames();
        // Waiting for decode must not pin completed external images behind GPU
        // retirement callbacks that would otherwise run only on a new submission.
        if (!drainedGpu) {
            const u64 waitStart = monotonic_ns();
            const Status ready = wait_export_gpu(gpu_->last_submitted_frame());
            if (!ready.ok()) return ready;
            // GPU work from the previous blurred frame is not decoder latency.
            const u64 waited = monotonic_ns() - waitStart;
            deadline += waited;
            hardDeadline += waited;
            drainedGpu = true;
        }
        rl.unlock();
        if (c.cancelRequested.load(std::memory_order_acquire)) return Errc::Cancelled;
        std::unique_lock<std::mutex> wl(exportWakeMutex_);
        exportWakeCv_.wait_for(wl, std::chrono::milliseconds(5), [&] {
            return mediaReadyGen_.load(std::memory_order_acquire) != gen
                || c.cancelRequested.load(std::memory_order_acquire);
        });
    }
    // Decoders de clipes que já passaram SAEM agora. Quem os aposentava era o
    // preview (render_frame → collect), que não roda durante o export: numa
    // timeline com vários clipes cada um deixava um codec de hardware aberto
    // (buffers, ImageReader, cache) até o fim. O aparelho esgotava as
    // instâncias de decoder, o clipe seguinte não abria e o export morria em
    // "quadros de video indisponiveis". Quem está na tela neste quadro acabou
    // de ser usado; só sai quem ficou 2 preparos sem aparecer.
    //
    // "Sem aparecer" não é "fora do trecho": o prepare pula a camada com
    // opacidade 0 (fade, flash) ou fora da tela ANTES de pedir o decoder. Sem
    // o toque abaixo, um fade de meio segundo fechava o codec no meio do clipe
    // e o reabria na volta — seek do keyframe, espera de até 4 s por quadro
    // (o "trava em 68%") e um fecha/abre de codec de hardware por fade, que é
    // justamente o que decoders de aparelho de entrada aguentam pior. Camada
    // de vídeo da composição principal ainda no seu trecho mantém o decoder.
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (const Composition* comp = project_ ? current_composition() : nullptr) {
            comp->layers().for_each([&](LayerId id, const Layer& l) {
                if (l.kind == LayerKind::Video && l.contains_time(t)) media_.touch(id, frameCounter_);
            });
        }
    }
    media_.collect(frameCounter_, kExportIdleFrames);
    const u64 t1 = monotonic_ns();
    FrameStats stats;
    RenderTimings timings;
    // Grava e submete: composição → NV12 → cópia para o slot, num frame só.
    // A GPU NÃO é esperada aqui (o produtor espera o quadro anterior depois).
    const Status s = renderer_.render(snapshot_, rs, &target, stats, timings);
    gpuFrame = gpu_->last_submitted_frame();
    if (rs.gpuTimers) {
        // O 1º quadro separado: é onde caem o IBL final e os pipelines do export.
        if (c.profiledFrames++ == 0) c.firstFrameMs = timings.cpuRecordMs;
        else c.recordMs += timings.cpuRecordMs;
        const GpuTiming* passes = nullptr;
        const u32 n = renderer_.last_gpu_passes(passes);
        if (n > 0) ++c.gpuFrames;
        for (u32 i = 0; i < n; ++i) {
            const char* label = passes[i].label ? passes[i].label : "?";
            auto it = std::find_if(c.gpuPasses.begin(), c.gpuPasses.end(),
                                   [&](const ExportContext::PassSum& p) { return std::strcmp(p.label, label) == 0; });
            if (it == c.gpuPasses.end()) c.gpuPasses.push_back({label, passes[i].ms});
            else it->ms += passes[i].ms;
        }
    }
    c.decodeNs.fetch_add(t1 - t0, std::memory_order_relaxed);
    c.renderNs.fetch_add(monotonic_ns() - t1, std::memory_order_relaxed);
    return s;
}

namespace {
/// O mixer lê do cache de export, decodificando o que faltar na hora.
class ExportBlocks final : public audio::BlockSource {
public:
    explicit ExportBlocks(audio::AudioBlockCache& c) : cache_(c) {}
    const audio::AudioBlock* block(u64 asset, i64 b) override {
        for (auto& h : recent_) {
            if (h.asset == asset && h.block == b) return h.ptr.get();
        }
        auto p = cache_.fetch(asset, b);
        if (!p) return nullptr;
        recent_.push_back(Held{asset, b, p});
        if (recent_.size() > 16) recent_.erase(recent_.begin());
        return recent_.back().ptr.get();
    }

private:
    struct Held {
        u64 asset;
        i64 block;
        std::shared_ptr<const audio::AudioBlock> ptr;
    };
    audio::AudioBlockCache& cache_;
    std::vector<Held> recent_;
};
} // namespace

Status Engine::write_export_audio(i64 untilSample) noexcept {
    ExportContext& ctx = *exportCtx_;
    ExportBlocks blocks(*ctx.audioCache);
    constexpr u32 kChunk = 4096;
    ctx.audioMix.resize(static_cast<usize>(kChunk) * audio::kMixChannels);
    ctx.audioPcm.resize(ctx.audioMix.size());
    while (ctx.audioWritten < untilSample) {
        const u32 n = static_cast<u32>(std::min<i64>(kChunk, untilSample - ctx.audioWritten));
        audio::mix(*ctx.audioSnap, ctx.audioWritten, n, blocks, ctx.audioMix.data(), nullptr, &ctx.audioFx);
        audio::to_pcm16(ctx.audioMix.data(), static_cast<usize>(n) * audio::kMixChannels, ctx.audioPcm.data());
        const i64 ptsUs = audio::sample_to_ns(ctx.audioWritten) / 1000;
        if (const Status s = ctx.sink->write_audio(ctx.audioPcm.data(), n, ptsUs); !s.ok()) return s;
        ctx.audioWritten += n;
    }
    return OkStatus;
}

void Engine::log_export_profile() noexcept {
    ExportContext& ctx = *exportCtx_;
    if (ctx.profiledFrames == 0) return;
    std::sort(ctx.gpuPasses.begin(), ctx.gpuPasses.end(),
              [](const ExportContext::PassSum& a, const ExportContext::PassSum& b) { return a.ms > b.ms; });
    const f64 kg = ctx.gpuFrames ? 1.0 / static_cast<f64>(ctx.gpuFrames) : 0.0;
    f64 gpuTotal = 0.0;
    for (const ExportContext::PassSum& p : ctx.gpuPasses) gpuTotal += p.ms;
    char top[512] = {};
    usize used = 0;
    for (usize i = 0; i < ctx.gpuPasses.size() && i < 8 && used < sizeof(top); ++i) {
        const int w = std::snprintf(top + used, sizeof(top) - used, "%s%s %.2f", i ? ", " : "",
                                    ctx.gpuPasses[i].label, ctx.gpuPasses[i].ms * kg);
        if (w <= 0) break;
        used += static_cast<usize>(w);
    }
    const f64 steady = ctx.profiledFrames > 1 ? ctx.recordMs / static_cast<f64>(ctx.profiledFrames - 1) : 0.0;
    AUREA_LOG_INFO("export perfil: %u q %ux%u | 1o quadro %.0f ms de CPU, depois %.2f ms/q | GPU %.2f ms/q: %s",
                   ctx.profiledFrames, ctx.width, ctx.height, ctx.firstFrameMs, steady, gpuTotal * kg, top);
}

void Engine::export_thread_main() noexcept {
    set_current_thread_name("aurea-export");
    ExportContext& ctx = *exportCtx_;
    OffscreenTarget target;
    target.texture = ctx.comp;
    target.width = ctx.width;
    target.height = ctx.height;
    target.yPlane = ctx.y;
    target.uvPlane = ctx.uv;
    target.encodeDither = ctx.dither;

    ctx.startNs = monotonic_ns();
    ctx.encoder = std::thread([this] { export_encoder_main(); });

    // Quadros submetidos à GPU e ainda não entregues ao encoder, em ordem.
    std::deque<u32> inflight;
    auto cancelled = [&] { return ctx.cancelRequested.load(std::memory_order_acquire); };
    // Espera a GPU terminar o quadro mais antigo em voo e o passa ao encoder.
    auto deliver_oldest = [&]() -> Status {
        const u32 si = inflight.front();
        ExportContext::Slot& slot = ctx.slots[si];
        const u64 r0 = monotonic_ns();
        {
            std::lock_guard<std::mutex> rl(renderMutex_);
            if (!gpu_) return Status{Errc::InvalidState, "sem GPU"};
            // A dense 3D frame can legitimately take longer than five seconds
            // on a mobile/software GPU. Poll in cancellable slices; keep a
            // finite deadline for a hung driver without dropping this frame.
            if (const Status s = wait_export_gpu(slot.gpuFrame); !s.ok()) return s;
            // Memória não coerente: invalida para a CPU ver o que a GPU
            // escreveu (o ponteiro é o mesmo; coerente = nada a fazer).
            void* p = nullptr;
            (void)gpu_->map_buffer(slot.y, p);
            (void)gpu_->map_buffer(slot.uv, p);
        }
        ctx.readNs.fetch_add(monotonic_ns() - r0, std::memory_order_relaxed);
        inflight.pop_front();
        {
            std::lock_guard<std::mutex> ql(ctx.qMutex);
            ctx.readySlots.push_back(si);
        }
        ctx.qCv.notify_all();
        return OkStatus;
    };

    Status result = OkStatus;
    for (u32 i = 0; i < ctx.frames && result.ok(); ++i) {
        if (cancelled()) { result = Errc::Cancelled; break; }
        // Calor (§37): um quadro em voo só — menos CPU e GPU ao mesmo tempo.
        // Resolução, fps, efeitos e amostras NÃO mudam.
        const bool hot = thermalDegrade_.load(std::memory_order_acquire);
        const u32 allowed = hot ? 1u : ctx.depth;
        const usize lag = (allowed > 1) ? 1u : 0u;   // quadros esperando o fence
        if (hot && ctx.thermalReducedFrames++ == 0) {
            AUREA_LOG_WARN("export: aparelho quente — 1 quadro em voo (qualidade igual)");
            std::lock_guard<std::mutex> pl(ctx.mutex);
            ctx.progress.flags |= kExportThermalReduced;
        }
        while (inflight.size() > lag && result.ok()) result = deliver_oldest();
        if (!result.ok()) break;

        // Um slot livre; todos em uso = o encoder ainda não devolveu (é o que
        // limita a memória e segura o ritmo).
        u32 si = 0;
        {
            std::unique_lock<std::mutex> ql(ctx.qMutex);
            ctx.qCv.wait(ql, [&] {
                return ctx.encoderFailed || cancelled()
                    || (!ctx.freeSlots.empty() && ctx.depth - static_cast<u32>(ctx.freeSlots.size()) < allowed);
            });
            if (ctx.encoderFailed) { result = ctx.encoderStatus; break; }
            if (cancelled()) { result = Errc::Cancelled; break; }
            si = ctx.freeSlots.back();
            ctx.freeSlots.pop_back();
        }
        ExportContext::Slot& slot = ctx.slots[si];
        slot.frame = i;
        target.yReadback = slot.y;
        target.uvReadback = slot.uv;
        // Tempo de SAÍDA → quadro da composição (fps diferentes se encontram
        // pelo instante, não pelo índice).
        const f64 seconds = static_cast<f64>(i) / ctx.fps;
        const FrameIndex t{static_cast<i64>(std::floor(seconds * ctx.compFps + 1e-6))};
        result = render_export_frame(t, target, slot.gpuFrame);
        if (!result.ok()) break;
        inflight.push_back(si);
    }
    // Fim da timeline: entrega o que ainda está na GPU.
    while (result.ok() && !inflight.empty()) result = deliver_oldest();
    {
        std::lock_guard<std::mutex> ql(ctx.qMutex);
        ctx.producerDone = true;
        if (!result.ok()) ctx.stop = true;
    }
    ctx.qCv.notify_all();
    ctx.encoder.join();
    if (result.ok() && ctx.encoderFailed) result = ctx.encoderStatus;
    // Cancelado enquanto o encoder esvaziava a fila: faltam quadros — nada de
    // finalizar um arquivo incompleto como se estivesse pronto.
    if (result.ok() && cancelled()) result = Errc::Cancelled;

    if (result.ok()) {
        result = ctx.sink->finish();
    } else {
        ctx.sink->abort();
    }
    ctx.sink.reset();
    log_export_profile();
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        if (gpu_) {
            gpu_->destroy_texture(ctx.comp);
            gpu_->destroy_texture(ctx.y);
            gpu_->destroy_texture(ctx.uv);
            for (ExportContext::Slot& s : ctx.slots) {
                gpu_->destroy_buffer(s.y);
                gpu_->destroy_buffer(s.uv);
                s.yPtr = s.uvPtr = nullptr;
            }
        }
    }
    {
        std::lock_guard<std::mutex> pl(ctx.mutex);
        ctx.progress.running = false;
        ctx.progress.finished = true;
        ctx.progress.result = result.code();
        if (result.ok()) ctx.set_message("concluido");
        else if (result.code() == Errc::Cancelled) ctx.set_message("cancelado");
        else ctx.set_message(result.detail().empty() ? result.message().data() : result.detail().data());
    }
    if (!result.ok() && result.code() != Errc::Cancelled) {
        AUREA_LOG_ERROR("export falhou [%d]: %s; %s", result.raw(), result.message().data(),
                       result.detail().empty() ? "" : result.detail().data());
    }
    exportActive_.store(false, std::memory_order_release);
    media_.proxies().set_pause_reason(PreviewProxyService::Export, false);
    forceRender_ = true;
    request_render();
}

void Engine::export_encoder_main() noexcept {
    set_current_thread_name("aurea-export-enc");
    ExportContext& ctx = *exportCtx_;
    std::unique_ptr<ai::Upscaler> upscaler;
    ai::TemporalStabilizer temporalUpscale;
    bool temporalAvailable = true;
    std::unique_ptr<u8[]> neuralInput, neuralOutput;
    if (ctx.settings.aiUpscale) {
        // Loading weights and all inference stay on the encoder worker.
        Status loaded = Errc::OutOfMemory;
        upscaler.reset(new (std::nothrow) ai::Upscaler());
        neuralInput.reset(new (std::nothrow) u8[static_cast<usize>(ctx.width) * ctx.height * 3]);
        neuralOutput.reset(new (std::nothrow) u8[static_cast<usize>(ctx.width * ctx.settings.aiUpscale) *
                                               (ctx.height * ctx.settings.aiUpscale) * 3 / 2]);
        if (upscaler && neuralInput && neuralOutput) {
            loaded = upscaler->load(ctx.settings.aiUpscale, 2, 64);
        }
        if (!loaded.ok()) {
            { std::lock_guard<std::mutex> ql(ctx.qMutex);
              ctx.encoderStatus = loaded; ctx.encoderFailed = true; ctx.stop = true; }
            ctx.qCv.notify_all();
            return;
        }
    }
    u32 sinceLog = 0;
    u64 logWrite = 0, logRead = 0, logRender = 0, logDecode = 0;
    for (;;) {
        u32 si = 0;
        {
            std::unique_lock<std::mutex> ql(ctx.qMutex);
            ctx.qCv.wait(ql, [&] {
                return ctx.stop || ctx.cancelRequested.load(std::memory_order_acquire) || !ctx.readySlots.empty()
                    || ctx.producerDone;
            });
            if (ctx.stop || ctx.cancelRequested.load(std::memory_order_acquire)) return;
            if (ctx.readySlots.empty()) return;   // produtor terminou e a fila esvaziou
            si = ctx.readySlots.front();
            ctx.readySlots.pop_front();
        }
        const ExportContext::Slot& slot = ctx.slots[si];
        const u32 i = slot.frame;
        const f64 seconds = static_cast<f64>(i) / ctx.fps;
        const i64 pts = static_cast<i64>(std::llround(seconds * 1e6));
        // Direto do buffer de leitura para o sink: nenhuma cópia intermediária.
        const u64 w0 = monotonic_ns();
        Status s;
        if (upscaler) {
            ai::nv12_to_rgb709(slot.yPtr, slot.uvPtr, ctx.width, ctx.height, neuralInput.get());
            struct Destination { ExportContext* exportContext; u8* pixels; u32 frame; } destination{&ctx, neuralOutput.get(), i};
            s = upscaler->run(neuralInput.get(), ctx.width, ctx.height, ctx.width * 3, ctx.cancelRequested,
                [](void* opaque, const ai::Upscaler::Tile& tile) {
                    auto& d = *static_cast<Destination*>(opaque);
                    auto& c = *d.exportContext;
                    if (!ai::rgb_tile_to_nv12_709(tile.rgb, tile.stride, tile.x, tile.y,
                        tile.width, tile.height, c.width * c.settings.aiUpscale,
                        c.height * c.settings.aiUpscale, d.pixels)) return false;
                    { std::lock_guard<std::mutex> lock(c.mutex);
                      std::snprintf(c.progress.message, sizeof(c.progress.message), "IA: %u/%u · %u%%", d.frame + 1,
                                    c.frames, static_cast<u32>(tile.progress * 100)); }
                    return !c.cancelRequested.load();
                }, &destination);
            if (s.ok() && !ai::resize_nv12_709(neuralOutput.get(), ctx.width * ctx.settings.aiUpscale,
                    ctx.height * ctx.settings.aiUpscale, neuralOutput.get(), ctx.outputWidth, ctx.outputHeight))
                s = Status{Errc::InvalidState, "dimensoes invalidas no upscale"};
            if (s.ok() && temporalAvailable) {
                const Status temporal = temporalUpscale.process(neuralInput.get(), ctx.width, ctx.height,
                    neuralOutput.get(), ctx.outputWidth, ctx.outputHeight, i);
                if (!temporal.ok()) {
                    temporalAvailable = false;
                    AUREA_LOG_WARN("AI temporal history disabled: memory budget or invalid dimensions");
                }
            }
            if (s.ok()) s = ctx.sink->write_video(neuralOutput.get(), ctx.outputWidth,
                neuralOutput.get() + static_cast<usize>(ctx.outputWidth) * ctx.outputHeight, ctx.outputWidth, pts);
        } else {
            s = ctx.sink->write_video(slot.yPtr, ctx.width, slot.uvPtr, ctx.width, pts);
        }
        const u64 w1 = monotonic_ns();
        ctx.writeNs.fetch_add(w1 - w0, std::memory_order_relaxed);
        // O som até o fim DESTE quadro (em amostras inteiras: nenhuma deriva
        // acumulada, nem em 29,97). Mesma thread do vídeo: o sink não é
        // chamado de duas threads.
        if (s.ok() && ctx.audioSnap) {
            s = write_export_audio(audio::frame_to_sample(static_cast<i64>(i) + 1, ctx.fps));
            ctx.audioNs.fetch_add(monotonic_ns() - w1, std::memory_order_relaxed);
        }
        {
            std::lock_guard<std::mutex> ql(ctx.qMutex);
            ctx.freeSlots.push_back(si);
            if (!s.ok()) {
                ctx.encoderFailed = true;
                ctx.encoderStatus = s;
                ctx.stop = true;
            }
        }
        ctx.qCv.notify_all();
        if (!s.ok()) return;

        const u32 done = i + 1;
        const f64 k = 1e-6 / static_cast<f64>(done);
        // Diagnóstico a cada 300 quadros: onde o tempo do export está indo.
        if (++sinceLog == 300 || done == ctx.frames) {
            // Disco quase cheio: para com uma mensagem clara em vez de o
            // muxer falhar no meio de uma escrita (ou o sistema apagar o
            // temporário). 16 MB: o que o índice final do MP4 ainda precisa.
            if (!ctx.outputDir.empty()) {
                std::error_code ec;
                const std::filesystem::space_info sp = std::filesystem::space(ctx.outputDir, ec);
                if (!ec && sp.available != static_cast<std::uintmax_t>(-1) && sp.available < (16ull << 20)) {
                    AUREA_LOG_ERROR("export: armazenamento cheio no quadro %u de %u", done, ctx.frames);
                    {
                        std::lock_guard<std::mutex> ql(ctx.qMutex);
                        ctx.encoderFailed = true;
                        ctx.encoderStatus = Status{Errc::StorageFull, "armazenamento cheio durante a exportacao"};
                        ctx.stop = true;
                    }
                    ctx.qCv.notify_all();
                    return;
                }
            }
            const u64 wr = ctx.writeNs.load(std::memory_order_relaxed), rd = ctx.readNs.load(std::memory_order_relaxed);
            const u64 rn = ctx.renderNs.load(std::memory_order_relaxed), dc = ctx.decodeNs.load(std::memory_order_relaxed);
            const f64 n = static_cast<f64>(sinceLog);
            AUREA_LOG_INFO("export %u/%u (%u em voo): decode %.1f render %.1f leitura %.1f encoder %.1f ms/quadro",
                           done, ctx.frames, ctx.depth, (dc - logDecode) / 1e6 / n, (rn - logRender) / 1e6 / n,
                           (rd - logRead) / 1e6 / n, (wr - logWrite) / 1e6 / n);
            logWrite = wr; logRead = rd; logRender = rn; logDecode = dc;
            sinceLog = 0;
        }
        const f64 elapsed = static_cast<f64>(monotonic_ns() - ctx.startNs) / 1e9;
        std::lock_guard<std::mutex> pl(ctx.mutex);
        ctx.progress.framesDone = done;
        ctx.progress.decodeWaitMs = static_cast<f32>(ctx.decodeNs.load(std::memory_order_relaxed) * k);
        ctx.progress.renderMs = static_cast<f32>(ctx.renderNs.load(std::memory_order_relaxed) * k);
        ctx.progress.readbackMs = static_cast<f32>(ctx.readNs.load(std::memory_order_relaxed) * k);
        ctx.progress.encodeMs = static_cast<f32>(ctx.writeNs.load(std::memory_order_relaxed) * k);
        ctx.progress.audioMs = static_cast<f32>(ctx.audioNs.load(std::memory_order_relaxed) * k);
        ctx.progress.fps = elapsed > 0.0 ? static_cast<f32>(done / elapsed) : 0.0f;
        ctx.progress.etaSeconds = ctx.progress.fps > 0.0f
                                ? static_cast<u32>((ctx.frames - done) / ctx.progress.fps) : 0;
    }
}

Status Engine::cancel_export() noexcept {
    if (!exportCtx_) return Errc::InvalidState;
    exportCtx_->cancelRequested.store(true, std::memory_order_release);
    // Acorda quem estiver esperando (slot livre, fila do encoder, decoder):
    // o cancelamento responde no próximo passo, não no fim de uma espera.
    {
        std::lock_guard<std::mutex> ql(exportCtx_->qMutex);
    }
    exportCtx_->qCv.notify_all();
    {
        std::lock_guard<std::mutex> wl(exportWakeMutex_);
    }
    exportWakeCv_.notify_all();
    return OkStatus;
}

Engine::ExportProgress Engine::export_progress() const noexcept {
    if (!exportCtx_) return ExportProgress{};
    std::lock_guard<std::mutex> pl(exportCtx_->mutex);
    return exportCtx_->progress;
}

// =============================================================================
// Export como imagem (export/ImageEncode.hpp): PNG do playhead, sequência PNG
// num .zip e GIF animado. O MESMO renderer, os mesmos quadros exatos e o mesmo
// progresso/cancelamento do vídeo — só a saída muda: a composição é lida em
// RGBA16F, convertida para sRGB de 8 bits e codificada aqui no motor.
// =============================================================================
ImageExportPlan Engine::query_image_export_plan(const ImageExportSettings& settings) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    const Composition* comp = current_composition();
    if (!comp) return ImageExportPlan{};
    return plan_image_export(settings, comp->width(), comp->height(), comp->fps(),
                             comp->export_duration(settings.trimToContent).value, comp->transparent_background());
}

Status Engine::start_image_export(const ImageExportSettings& settings, const char* outputPath) noexcept {
    if (!project_) return Errc::InvalidState;
    if (!outputPath || !*outputPath) return Errc::InvalidArgument;
    if (static_cast<u32>(settings.format) > static_cast<u32>(ImageExportFormat::Gif)) return Errc::InvalidArgument;
    if (!gpu_ || !renderer_.ready()) return Status{Errc::NotSupported, "export precisa de GPU"};
    if (exportCtx_ && exportCtx_->thread.joinable()) {
        bool running = false;
        {
            std::lock_guard<std::mutex> pl(exportCtx_->mutex);
            running = exportCtx_->progress.running;
        }
        if (running) return Status{Errc::InvalidState, "ja existe um export em andamento"};
        exportCtx_->thread.join();
    }

    auto ctx = std::make_unique<ExportContext>();
    ctx->imageMode = true;
    ctx->image = settings;
    ctx->outputPath = outputPath;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        Composition* comp = current_composition();
        if (!comp) return Errc::NotFound;
        const ImageExportPlan plan = plan_image_export(settings, comp->width(), comp->height(), comp->fps(),
                                                       comp->export_duration(settings.trimToContent).value,
                                                       comp->transparent_background());
        if (plan.width == 0 || plan.height == 0 || plan.frames == 0) return Errc::InvalidState;
        ctx->compFps = comp->fps();
        ctx->fps = plan.fps;
        ctx->width = ctx->outputWidth = plan.width;
        ctx->height = ctx->outputHeight = plan.height;
        ctx->frames = plan.frames;
        ctx->imageAlpha = plan.alpha;
        ctx->imageFrame = project_->timeline().playhead();
    }
    if (settings.format == ImageExportFormat::Gif && ctx->frames > kGifMaxFrames)
        return Status{Errc::NotSupported, "GIF longo demais: use um trecho menor ou menos quadros por segundo"};
    if (settings.format == ImageExportFormat::PngSequence && ctx->frames > kSequenceMaxFrames)
        return Status{Errc::NotSupported, "sequencia longa demais: use um trecho menor ou menos quadros por segundo"};
    const u32 maxTex = gpu_->capabilities().maxTexture2D;
    if (maxTex > 0 && std::max(ctx->width, ctx->height) > maxTex)
        return Status{Errc::NotSupported, "resolucao acima do limite de textura da GPU"};
    // Memória da CPU: leitura em meia-precisão + RGBA8 + o PNG/GIF do quadro.
    const u64 working = static_cast<u64>(ctx->width) * ctx->height * 16 + (16ull << 20);
    if (working > std::max<u64>(64ull << 20, caps_.memory_budget_bytes() / 2))
        return Status{Errc::OutOfMemory, "memoria insuficiente para esta resolucao"};
    // Espaço em disco antes de começar (a mesma regra do vídeo: recusa só
    // quando nem metade da estimativa cabe).
    {
        const u64 estimate = estimate_image_export_bytes(settings.format, ctx->width, ctx->height, ctx->frames, ctx->imageAlpha);
        std::error_code ec;
        std::filesystem::path dir = std::filesystem::u8path(outputPath).parent_path();
        if (dir.empty()) dir = std::filesystem::current_path(ec);
        const std::filesystem::space_info sp = std::filesystem::space(dir, ec);
        ctx->estimatedBytes = estimate;
        if (!ec && sp.available != static_cast<std::uintmax_t>(-1) && sp.available < estimate / 2 + (16ull << 20)) {
            static thread_local char detail[160];   // Status guarda o ponteiro
            std::snprintf(detail, sizeof(detail), "espaco livre insuficiente: a imagem precisa de ~%llu MB e ha %llu MB livres",
                          static_cast<unsigned long long>(estimate >> 20), static_cast<unsigned long long>(sp.available >> 20));
            return Status{Errc::StorageFull, detail};
        }
        ctx->outputDir = dir;
    }
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        TextureDesc cd;
        cd.width = ctx->width;
        cd.height = ctx->height;
        cd.format = SurfaceFormat::RGBA16F;
        cd.sampled = true;
        cd.renderTarget = true;
        cd.transferSrc = true;
        cd.debugName = "export-imagem";
        auto c = gpu_->create_texture(cd);
        if (!c.ok()) return Status{Errc::OutOfMemory, "sem memoria de GPU para o export"};
        ctx->comp = *c;
    }
    struct ProxyExportPause {
        PreviewProxyService& service;
        bool started = false;
        ~ProxyExportPause() { if (!started) service.set_pause_reason(PreviewProxyService::Export, false); }
    } proxyPause{media_.proxies()};
    media_.proxies().set_pause_reason(PreviewProxyService::Export, true);

    ctx->progress.running = true;
    ctx->progress.framesTotal = ctx->frames;
    ctx->progress.pipelineDepth = 1;
    ctx->depth = 1;
    ctx->set_message("exportando");
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        playback_.pause(monotonic_ns());
        playingHint_ = false;
        audio_.stop();
    }
    exportCtx_ = std::move(ctx);
    exportActive_.store(true, std::memory_order_release);
    media_.proxies().set_pause_reason(PreviewProxyService::Playback, false);
    proxyPause.started = true;
    exportCtx_->thread = std::thread([this] { image_export_thread_main(); });
    return OkStatus;
}

void Engine::image_export_thread_main() noexcept {
    set_current_thread_name("aurea-export-img");
    ExportContext& ctx = *exportCtx_;
    const ImageExportFormat format = ctx.image.format;
    OffscreenTarget target;
    target.texture = ctx.comp;
    target.width = ctx.width;
    target.height = ctx.height;
    ctx.startNs = monotonic_ns();
    const usize pixels = static_cast<usize>(ctx.width) * ctx.height;
    std::vector<u16> half(pixels * 4);
    std::vector<u8> rgba(pixels * 4);
    std::vector<u8> png;
    GifWriter gif;
    StoredZipWriter zip;
    auto cancelled = [&] { return ctx.cancelRequested.load(std::memory_order_acquire); };

    Status result = OkStatus;
    if (format == ImageExportFormat::Gif) {
        result = gif.open(ctx.outputPath.c_str(), ctx.width, ctx.height, ctx.imageAlpha, ctx.image.dither);
    } else if (format == ImageExportFormat::PngSequence) {
        result = zip.open(ctx.outputPath.c_str());
    }
    for (u32 i = 0; i < ctx.frames && result.ok(); ++i) {
        if (cancelled()) { result = Errc::Cancelled; break; }
        // Tempo de SAÍDA → quadro da composição (como no vídeo). O PNG é o
        // quadro do playhead.
        const FrameIndex t = format == ImageExportFormat::Png
            ? ctx.imageFrame
            : FrameIndex{static_cast<i64>(std::floor(static_cast<f64>(i) / ctx.fps * ctx.compFps + 1e-6))};
        u64 gpuFrame = 0;
        result = render_export_frame(t, target, gpuFrame);
        if (!result.ok()) break;
        const u64 r0 = monotonic_ns();
        {
            std::lock_guard<std::mutex> rl(renderMutex_);
            if (!gpu_) { result = Status{Errc::InvalidState, "sem GPU"}; break; }
            result = wait_export_gpu(gpuFrame);
            if (result.ok()) result = gpu_->read_texture(ctx.comp, half.data(), ctx.width * 8);
        }
        if (!result.ok()) break;
        linear_half_to_srgb8(half.data(), pixels, rgba.data());
        const u64 w0 = monotonic_ns();
        ctx.readNs.fetch_add(w0 - r0, std::memory_order_relaxed);
        switch (format) {
            case ImageExportFormat::Png: {
                result = encode_png(rgba.data(), ctx.width, ctx.height, ctx.imageAlpha, png);
                if (!result.ok()) break;
                std::FILE* f = std::fopen(ctx.outputPath.c_str(), "wb");
                if (!f) { result = Status{Errc::IoError, "nao foi possivel criar o PNG"}; break; }
                const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size() && std::fflush(f) == 0;
                std::fclose(f);
                if (!ok) result = Status{Errc::StorageFull, "falha ao gravar o PNG (armazenamento cheio?)"};
                break;
            }
            case ImageExportFormat::PngSequence: {
                result = encode_png(rgba.data(), ctx.width, ctx.height, ctx.imageAlpha, png);
                char name[32];
                std::snprintf(name, sizeof(name), "frame_%05u.png", i + 1);
                if (result.ok()) result = zip.add(name, png.data(), png.size());
                break;
            }
            case ImageExportFormat::Gif:
                result = gif.add_frame(rgba.data(), gif_frame_delay_cs(i, ctx.fps));
                break;
        }
        ctx.writeNs.fetch_add(monotonic_ns() - w0, std::memory_order_relaxed);
        if (!result.ok()) break;
        const u32 done = i + 1;
        const f64 elapsed = static_cast<f64>(monotonic_ns() - ctx.startNs) / 1e9;
        const f64 k = 1e-6 / static_cast<f64>(done);
        std::lock_guard<std::mutex> pl(ctx.mutex);
        ctx.progress.framesDone = done;
        ctx.progress.decodeWaitMs = static_cast<f32>(ctx.decodeNs.load(std::memory_order_relaxed) * k);
        ctx.progress.renderMs = static_cast<f32>(ctx.renderNs.load(std::memory_order_relaxed) * k);
        ctx.progress.readbackMs = static_cast<f32>(ctx.readNs.load(std::memory_order_relaxed) * k);
        ctx.progress.encodeMs = static_cast<f32>(ctx.writeNs.load(std::memory_order_relaxed) * k);
        ctx.progress.fps = elapsed > 0.0 ? static_cast<f32>(done / elapsed) : 0.0f;
        ctx.progress.etaSeconds = ctx.progress.fps > 0.0f ? static_cast<u32>((ctx.frames - done) / ctx.progress.fps) : 0;
    }
    if (result.ok() && cancelled()) result = Errc::Cancelled;
    if (format == ImageExportFormat::Gif) {
        if (result.ok()) result = gif.finish();
        else gif.abort();
    } else if (format == ImageExportFormat::PngSequence) {
        if (result.ok()) result = zip.finish();
        else zip.abort();
    } else if (!result.ok()) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(ctx.outputPath), ec);
    }
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        if (gpu_) gpu_->destroy_texture(ctx.comp);
    }
    {
        std::lock_guard<std::mutex> pl(ctx.mutex);
        ctx.progress.running = false;
        ctx.progress.finished = true;
        ctx.progress.result = result.code();
        if (result.ok()) ctx.set_message("concluido");
        else if (result.code() == Errc::Cancelled) ctx.set_message("cancelado");
        else ctx.set_message(result.detail().empty() ? result.message().data() : result.detail().data());
    }
    if (!result.ok() && result.code() != Errc::Cancelled) {
        AUREA_LOG_ERROR("export de imagem falhou [%d]: %s; %s", result.raw(), result.message().data(),
                        result.detail().empty() ? "" : result.detail().data());
    } else if (result.ok()) {
        AUREA_LOG_INFO("export de imagem: formato %u, %u quadro(s) %ux%u em %.2f s", static_cast<u32>(format), ctx.frames,
                       ctx.width, ctx.height, static_cast<f64>(monotonic_ns() - ctx.startNs) / 1e9);
    }
    exportActive_.store(false, std::memory_order_release);
    media_.proxies().set_pause_reason(PreviewProxyService::Export, false);
    forceRender_ = true;
    request_render();
}

// =============================================================================
// Aplicação de comandos
// =============================================================================
Status Engine::apply_command(const Command& cmd, const char* stringData) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Status s = apply_command_internal(cmd, stringData, true);
    if (s.ok() && (mutates_model(cmd.type) || cmd.type == CommandType::CompositionCreate || cmd.type == CommandType::CompositionDelete)) project_->mark_dirty();
    request_render();
    return s;
}

bool Engine::mutates_model(CommandType type) noexcept {
    const auto t = static_cast<u16>(type);
    // Tudo que muda a composição: camadas, keyframes, máscaras, efeitos,
    // áudio, texto e ajustes da composição. Reprodução, visualização,
    // histórico, export, troca de composição e 3D (não implementado) não.
    return (t >= static_cast<u16>(CommandType::LayerCreate) && t <= static_cast<u16>(CommandType::TextSetStrokeColor))
        || type == CommandType::CompositionSetSize || type == CommandType::CompositionSetFps
        || type == CommandType::CompositionSetDuration || type == CommandType::CompositionSetBackground
        || type == CommandType::AudioSetVolume || type == CommandType::AudioSetPan
        || type == CommandType::LayerSetSpeed || type == CommandType::LayerSetReversed
        || type == CommandType::ShapeSetFill || type == CommandType::ShapeSetStroke
        || type == CommandType::ShapeSetParam || type == CommandType::LayerLayoutTransform || type == CommandType::LayerSetLightParam || type == CommandType::LayerSetMaterialParam
        || type == CommandType::LayerSetCameraParam
        || type == CommandType::LayerSetMagnetic;
}

void Engine::record_history_locked(CommandType type) noexcept {
    if (!mutates_model(type) || !project_) return;
    const CompositionId id = project_->timeline().current();
    if (const Composition* c = project_->timeline().composition(id)) {
        history_.before_mutation(*c, id, "editar");
    }
}

void Engine::after_history_restore_locked() noexcept {
    Composition* comp = current_composition();
    if (!comp) return;
    std::erase_if(selection_, [&](u64 id) { return comp->layer(LayerId::unpack(id)) == nullptr; });
    playback_.configure(comp->fps(), comp->duration());
    adapt().configure(comp->width(), comp->height(), static_cast<f32>(comp->fps()));
    project_->mark_dirty();
    request_render();
}

namespace {

// -----------------------------------------------------------------------------
// Validação do comando ANTES de mexer no modelo (§134, fuzz de comandos).
//
// O comando atravessa a bridge como bytes crus: um enum fora da faixa (camada
// "tipo 999", modo de mistura 300) chegava ao renderer como índice de tabela,
// e um NaN/inf num transform envenenava a matriz de todos os filhos. A UI não
// manda isso — mas um bug de layout do lado Kotlin ou memória corrompida
// mandaria, e a resposta certa é recusar o comando, não desenhar lixo.
// -----------------------------------------------------------------------------
constexpr i64 kMaxCommandFrame = i64{1} << 40;   // ~1100 anos a 30 fps: acima disso é lixo

bool finite(f32 v) noexcept { return std::isfinite(v); }
bool finite_all(std::initializer_list<f32> vs) noexcept {
    for (f32 v : vs) if (!std::isfinite(v)) return false;
    return true;
}
bool frame_ok(FrameIndex f) noexcept { return f.value > -kMaxCommandFrame && f.value < kMaxCommandFrame; }
bool track_ok(const TrackRef& t) noexcept { return static_cast<u16>(t.property) < static_cast<u16>(TrackProperty::_Count); }

bool command_valid(const Command& c) noexcept {
    switch (c.type) {
        case CommandType::LayerCreate:
            return static_cast<u16>(c.layer_create.kind) <= static_cast<u16>(LayerKind::Composition);
        case CommandType::LayerSetBlendMode:
            return static_cast<u16>(c.layer_blend.mode) <= static_cast<u16>(BlendMode::LinearBurn);
        case CommandType::LayerSetTimeRange:
            // `offset` só conta com `setOffset` (a UI e os testes deixam o resto do payload sem valor).
            return frame_ok(c.layer_range.start) && frame_ok(c.layer_range.end)
                && (!c.layer_range.setOffset || frame_ok(c.layer_range.offset));
        case CommandType::LayerSplit:
            return frame_ok(c.layer_split.at);
        case CommandType::LayerSetTransform: {
            const TransformPayload& t = c.transform;
            return finite_all({t.x, t.y, t.z, t.sx, t.sy, t.sz, t.rx, t.ry, t.rz, t.ax, t.ay, t.az, t.opacity});
        }
        case CommandType::LayerSetPosition: return finite_all({c.position.x, c.position.y, c.position.z});
        case CommandType::LayerSetScale:    return finite_all({c.scale.sx, c.scale.sy, c.scale.sz});
        case CommandType::LayerSetRotation: return finite_all({c.rotation.rx, c.rotation.ry, c.rotation.rz});
        case CommandType::LayerSetAnchor:   return finite_all({c.anchor.ax, c.anchor.ay, c.anchor.az});
        case CommandType::LayerSetOpacity:  return finite(c.opacity.opacity);
        case CommandType::LayerSetSkew:     return finite_all({c.skew.skewX, c.skew.skewY});
        case CommandType::KeyframeInsert:
        case CommandType::KeyframeSetValue:
            return track_ok(c.keyframe.track) && frame_ok(c.keyframe.time) && finite(c.keyframe.value);
        case CommandType::KeyframeDelete:
            return track_ok(c.keyframe.track) && frame_ok(c.keyframe.time);
        case CommandType::KeyframeMove:
            return track_ok(c.keyframe_move.track) && frame_ok(c.keyframe_move.fromTime) && frame_ok(c.keyframe_move.toTime);
        case CommandType::KeyframeSetInterpolation:
        case CommandType::KeyframeSetBezier:
        case CommandType::KeyframeSetEasing: {
            const KeyframeInterpPayload& k = c.keyframe_interp;
            return track_ok(k.track) && frame_ok(k.time)
                && static_cast<u8>(k.interp) <= static_cast<u8>(Interpolation::Steps)
                && k.power <= 3
                && finite_all({k.bx1, k.by1, k.bx2, k.by2});
        }
        case CommandType::MaskSetOperation:
            return static_cast<u8>(c.mask_op.op) <= static_cast<u8>(MaskOperation::None);
        case CommandType::MaskSetFeather:
        case CommandType::MaskSetExpansion:
        case CommandType::MaskSetOpacity:
            return finite(c.mask_scalar.value);
        case CommandType::MaskSetPath: {
            const MaskPointPayload& m = c.mask_point;
            return finite_all({m.x, m.y, m.inX, m.inY, m.outX, m.outY});
        }
        case CommandType::EffectSetParam:       return finite(c.effect_param.value);
        case CommandType::EffectSetColorParam:  return finite_all({c.effect_color.r, c.effect_color.g, c.effect_color.b, c.effect_color.a});
        case CommandType::AudioSetGain:
        case CommandType::AudioSetVolume:
        case CommandType::AudioSetPan:
        case CommandType::LayerSetSpeed:        return finite(c.audio_gain.gain);
        case CommandType::AudioSetFadeIn:
        case CommandType::AudioSetFadeOut:      return frame_ok(c.audio_fade.duration);
        case CommandType::ShapeSetParam:        return finite(c.shape_param.value);
        case CommandType::LayerSetMaterialParam: return c.material_param.param < 6 && finite(c.material_param.value) && c.material_param.value >= 0 && c.material_param.value <= 1;
        case CommandType::LayerSetLightParam: {
            const u32 p = c.shape_param.param;
            const f32 v = c.shape_param.value;
            const f32 maximum = p == 0 ? 2.f : (p >= 2 && p <= 4) || p == 7 || p == 10 ? 1.f
                              : p == 6 ? 179.f : p == 9 ? .1f : 1000000.f;
            return p <= 10 && finite(v) && v >= 0 && v <= maximum;
        }
        case CommandType::LayerSetCameraParam: return c.shape_param.param < 5 && finite(c.shape_param.value);
        case CommandType::LayerLayoutTransform: return c.shape_param.param < 15 && finite(c.shape_param.value);
        case CommandType::ShapeSetFill:
        case CommandType::ShapeSetStroke:
        case CommandType::TextSetColor:
        case CommandType::TextSetStrokeColor:   return finite_all({c.text_color.r, c.text_color.g, c.text_color.b, c.text_color.a});
        case CommandType::TextSetSize:          return finite(c.text_size.size);
        case CommandType::TextSetStrokeWidth:   return finite(c.text_stroke_width.width);
        case CommandType::CompositionSetFps:    return std::isfinite(c.comp_fps.fps) && c.comp_fps.fps > 0.0 && c.comp_fps.fps <= 1000.0;
        case CommandType::CompositionSetDuration: return frame_ok(c.comp_duration.duration);
        case CommandType::CompositionSetBackground:
            return finite_all({c.comp_background.r, c.comp_background.g, c.comp_background.b, c.comp_background.a});
        case CommandType::ViewportSetZoom:      return finite(c.viewport_zoom.zoom);
        case CommandType::ViewportSetPan:       return finite_all({c.viewport_pan.x, c.viewport_pan.y});
        case CommandType::PlaybackSetSpeed:     return finite(c.speed.speed);
        default:
            return true;
    }
}

} // namespace

Status Engine::apply_command_internal(const Command& cmd, const char* stringData,
                                      bool recordUndo) noexcept {
    if (!project_) return Errc::InvalidState;
    // Antes do snapshot de desfazer: comando inválido não vira ação vazia no histórico.
    if (!command_valid(cmd)) return Status{Errc::InvalidArgument, "comando com valor invalido"};

    Timeline& timeline = project_->timeline();
    Composition* comp = current_composition();
    const u64 now = monotonic_ns();

    auto need_layer = [&](LayerId id) -> Layer* { return comp ? comp->layer(id) : nullptr; };
    if (cmd.type == CommandType::KeyframeInsert && cmd.keyframe.onlyIfChanged) {
        const Layer* layer = need_layer(cmd.keyframe.track.layer);
        if (!layer) return Errc::NotFound;
        const u32 property = static_cast<u32>(cmd.keyframe.track.property);
        if (property <= static_cast<u32>(TrackProperty::SkewY)) {
            const Transform& t = layer->transform;
            const f32 base[] = {t.position.x, t.position.y, t.position.z,
                t.scale.x, t.scale.y, t.scale.z, t.rotation.x, t.rotation.y, t.rotation.z,
                t.anchor.x, t.anchor.y, t.anchor.z, t.opacity, t.skewX, t.skewY};
            const Track* track = layer->tracks.find(cmd.keyframe.track.property);
            const f32 current = track ? track->value_or(cmd.keyframe.time, base[property]) : base[property];
            const f32 value = cmd.keyframe.value;
            const f32 tolerance = 4.0f * std::numeric_limits<f32>::epsilon() * std::max({1.0f, std::abs(current), std::abs(value)});
            // Check before creating a track, recording undo or invalidating the model.
            if (std::abs(current - value) <= tolerance) return OkStatus;
        }
    }
    if (cmd.type == CommandType::EffectAdd && cmd.effect_add.effectType == effect_type_id(effect_keys::kText3DLayout)) {
        const Layer* layer = need_layer(cmd.effect_add.layer);
        const Asset* source = layer ? project_->asset(layer->model.scene) : nullptr;
        scene3d::Text3DSpec spec;
        if (!layer || layer->locked || !source || layer->kind != LayerKind::Model3D || !scene3d::decode_text3d(source->sourcePath, spec))
            return Status{Errc::InvalidArgument, "selecione texto 3D desbloqueado"};
    }
    if (cmd.type == CommandType::EffectAdd && cmd.effect_add.effectType == effect_type_id(effect_keys::kShape3DLayout)) {
        const Layer* layer = need_layer(cmd.effect_add.layer);
        if (!layer || layer->locked || !is_shape3d_layer(*layer))
            return Status{Errc::InvalidArgument, "selecione uma forma 3D desbloqueada"};
    }
    // A lock protects timeline edits at the shared boundary, including queued
    // commands after a gesture has started. Reject before recording undo.
    if (cmd.type == CommandType::LayerDelete || cmd.type == CommandType::LayerSetTimeRange ||
        cmd.type == CommandType::LayerSplit) {
        if (const Layer* layer = need_layer(cmd.layer_ref.layer); layer && layer->locked)
            return Status{Errc::InvalidState, "camada bloqueada"};
    }
    // O PlaybackController é a fonte da verdade; a timeline espelha o estado
    // para quem a consulta (serialização, estado da UI).
    auto sync_timeline = [&] {
        const bool playing = playback_.playing();
        if (playingHint_.exchange(playing) != playing) media_.proxies().set_pause_reason(PreviewProxyService::Playback, playing);
        if (playback_.playing()) timeline.play();
        else timeline.pause();
        timeline.set_playhead(playback_.current());
    };

    if (recordUndo) record_history_locked(cmd.type);
    if (mutates_model(cmd.type) || cmd.type == CommandType::Undo || cmd.type == CommandType::Redo) {
        modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    }

    switch (cmd.type) {
        // ---------------------------------------------------------------------
        // Camadas
        // ---------------------------------------------------------------------
        case CommandType::LayerCreate: {
            if (!comp) return Errc::InvalidState;
            const LayerId id = comp->add_layer(cmd.layer_create.kind,
                                               stringData ? std::string(stringData) : std::string{});
            if (!id.valid()) return Errc::OutOfMemory;
            return OkStatus;
        }

        case CommandType::LayerDelete: {
            if (!comp) return Errc::InvalidState;
            if (!need_layer(cmd.layer_ref.layer)) return Errc::NotFound;
            media_.close_layer(cmd.layer_ref.layer);
            comp->remove_layer(cmd.layer_ref.layer);
            selection_.erase(std::remove(selection_.begin(), selection_.end(), cmd.layer_ref.layer.pack()),
                             selection_.end());
            return OkStatus;
        }

        case CommandType::LayerDuplicate: {
            if (!comp) return Errc::InvalidState;
            // A cópia entra LOGO ACIMA do original na ordem do Core — e é essa
            // ordem que a composição desenha (teste de regressão em test_engine).
            const LayerId id = comp->duplicate_layer(cmd.layer_ref.layer, timeline.playhead());
            if (!id.valid()) return Errc::OutOfMemory;
            // The next gesture belongs to the copy. Update selection with creation,
            // before either native UI has had time to refresh its layer snapshot.
            const auto source = std::find(selection_.begin(), selection_.end(), cmd.layer_ref.layer.pack());
            if (source != selection_.end()) *source = id.pack();
            else selection_.assign(1, id.pack());
            std::sort(selection_.begin(), selection_.end());
            return OkStatus;
        }

        case CommandType::LayerReorder: {
            if (!comp) return Errc::InvalidState;
            if (!comp->reorder_layer(cmd.layer_reorder.layer, cmd.layer_reorder.newIndex)) return Errc::OutOfRange;
            return OkStatus;
        }

        case CommandType::LayerSetName: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            if (stringData) l->name = stringData;
            return OkStatus;
        }

        case CommandType::LayerSetTimeRange: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            if (cmd.layer_range.end.value <= cmd.layer_range.start.value) return Errc::InvalidArgument;
            const auto extend_duration = [&] {
                if (!comp) return;
                i64 end = comp->duration().value;
                comp->layers().for_each([&](LayerId, const Layer& item) { end = std::max(end, item.end.value); });
                if (end > comp->duration().value) {
                    comp->set_duration(FrameIndex{end});
                    playback_.configure(comp->fps(), comp->duration());
                    sync_timeline();
                }
            };
            if (comp && comp->edit_mode()) {
                // Modo Edição (ímã): aparar não abre nem sobrepõe — quem vem
                // depois anda junto. Mover (início e fim juntos) fica livre.
                const i64 oldStart = l->start.value, oldEnd = l->end.value;
                const i64 ds = cmd.layer_range.start.value - oldStart, de = cmd.layer_range.end.value - oldEnd;
                const LayerId self = cmd.layer_ref.layer;
                if (ds == 0 && de != 0) {
                    l->end = cmd.layer_range.end;
                    if (cmd.layer_range.setOffset) l->offset = cmd.layer_range.offset;
                    comp->shift_from(FrameIndex{oldEnd}, de, self);
                    extend_duration();
                    return OkStatus;
                }
                if (ds != 0 && de == 0) {
                    // Aparar o começo: a camada fica no lugar (encostada na
                    // anterior), perde o trecho no FIM da posição, e quem vem
                    // depois recua o mesmo tanto.
                    const i64 len = oldEnd - cmd.layer_range.start.value;
                    if (len <= 0) return Errc::InvalidArgument;
                    l->end = FrameIndex{oldStart + len};
                    if (cmd.layer_range.setOffset) l->offset = cmd.layer_range.offset;
                    comp->shift_from(FrameIndex{oldEnd}, -ds, self);
                    extend_duration();
                    return OkStatus;
                }
            }
            l->start = cmd.layer_range.start;
            l->end = cmd.layer_range.end;
            if (cmd.layer_range.setOffset) l->offset = cmd.layer_range.offset;
            extend_duration();
            return OkStatus;
        }

        case CommandType::LayerSplit: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l || !comp) return Errc::NotFound;
            const FrameIndex at = cmd.layer_split.at;
            if (at.value <= l->start.value || at.value >= l->end.value) return Errc::OutOfRange;
            const FrameIndex originalStart = l->start, originalEnd = l->end, originalOffset = l->offset;
            const LayerId second = comp->duplicate_layer(cmd.layer_ref.layer, at);
            if (!second.valid()) return Errc::OutOfMemory;
            l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            const f64 sourceLimit = clip_source_limit(*project_, *comp, *l);
            trim_clip_copy(*l, originalStart.value, at.value, sourceLimit);
            if (Layer* s = comp->layer(second)) {
                s->start = originalStart; s->end = originalEnd; s->offset = originalOffset;
                trim_clip_copy(*s, at.value, originalEnd.value, sourceLimit);
            }
            return OkStatus;
        }

        case CommandType::LayerSetVisible: {
            Layer* l = need_layer(cmd.layer_visible.layer);
            if (!l) return Errc::NotFound;
            l->visible = cmd.layer_visible.visible;
            return OkStatus;
        }

        case CommandType::LayerSetLocked: {
            Layer* l = need_layer(cmd.layer_locked.layer);
            if (!l) return Errc::NotFound;
            l->locked = cmd.layer_locked.locked;
            return OkStatus;
        }

        case CommandType::LayerSetMagnetic: {
            Layer* l = need_layer(cmd.layer_magnetic.layer);
            if (!l) return Errc::NotFound;
            l->magneticTrack = cmd.layer_magnetic.magnetic;
            return OkStatus;
        }

        case CommandType::LayerSetParent: {
            Layer* l = need_layer(cmd.layer_parent.layer);
            if (!l) return Errc::NotFound;
            // Both mobile UIs use zero for "None"; zero is not the C++ invalid
            // handle sentinel. Normalize it before validating or compensating.
            const LayerId parent = cmd.layer_parent.parent.pack() == 0 ? LayerId{} : cmd.layer_parent.parent;
            if (parent.valid()) {
                if (!comp || !comp->layer(parent)) return Errc::NotFound;
                if (parent == cmd.layer_parent.layer) return Errc::InvalidArgument;
                LayerId cursor = parent;
                for (u32 depth = 0; depth < kMaxNestingDepth * 4 && cursor.valid(); ++depth) {
                    if (cursor == cmd.layer_parent.layer) return Errc::InvalidArgument;
                    const Layer* p = comp->layer(cursor);
                    if (!p) break;
                    cursor = p->parent;
                }
            }
            // Compensação: o filho fica onde está na tela ao ganhar, trocar ou
            // perder o pai. M = (pai novo)⁻¹ × (pai antigo) leva o espaço do pai
            // antigo para o do novo; os valores parados viram "M × local" e os
            // keyframes animados passam pela MESMA M (a animação continua igual
            // na tela). Modelo 3D, câmera e luz: tudo pelo mundo 3D.
            if (!comp) { l->parent = parent; return OkStatus; }
            const FrameIndex now = playback_.current();
            const LayerId oldParent = l->parent;
            const bool wasThreeD = l->threeD;
            const bool in3d = l->kind == LayerKind::Model3D || l->kind == LayerKind::Camera || l->kind == LayerKind::Light
                           || l->threeD || wants_layer_3d(*comp, *l, now);
            auto worldOf = [&](const Layer& x) { return in3d ? layer_world_3d(*comp, x, now) : layer_world_matrix(*comp, x, now); };
            const Layer* oldP = l->parent.valid() ? comp->layer(l->parent) : nullptr;
            const Mat4 oldPw = oldP ? worldOf(*oldP) : Mat4::identity();
            const Mat4 world = worldOf(*l);
            l->parent = parent;
            // Detaching must not switch an inherited 3D plane to the 2D
            // compositor, which would bypass the scene camera and make it jump.
            if (oldP && in3d) l->threeD = true;
            const Layer* newP = parent.valid() ? comp->layer(parent) : nullptr;
            // Pai novo pode puxar a camada para o 3D (pai 3D): mede no mesmo espaço.
            const bool now3d = in3d || (newP && (newP->threeD || wants_layer_3d(*comp, *l, now)));
            const Mat4 newPw = newP ? (now3d ? layer_world_3d(*comp, *newP, now) : layer_world_matrix(*comp, *newP, now))
                                    : Mat4::identity();
            const Mat4 M = inverse4(newPw) * oldPw;
            if (now3d || l->hasParentBasis) {
                // Preserve the complete bind space, not a lossy TRS
                // decomposition. This also preserves animated rotations on
                // every frame and the entire subtree under a nested 3D null.
                const Vec3 x{newPw.col[0].x, newPw.col[0].y, newPw.col[0].z};
                const Vec3 y{newPw.col[1].x, newPw.col[1].y, newPw.col[1].z};
                const Vec3 z{newPw.col[2].x, newPw.col[2].y, newPw.col[2].z};
                const f32 determinant = x.dot(y.cross(z));
                if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-12f) {
                    l->parent = oldParent;
                    l->threeD = wasThreeD;
                    return Errc::InvalidArgument;
                }
                l->parentBasis = M * l->parentBasis;
                // Cofactor inversion can leave roundoff in the affine row.
                l->parentBasis.col[0].w = l->parentBasis.col[1].w = l->parentBasis.col[2].w = 0;
                l->parentBasis.col[3].w = 1;
                l->hasParentBasis = true;
                l->threeD = true;
                return OkStatus;
            }
            // Keyframes: posição como ponto (M inteira), rotação Z mais o giro
            // de M, escala vezes a escala de M. Tudo lido ANTES de reescrever.
            const auto localNow = l->local_time(now);
            auto sample = [&](TrackProperty prop, f32 fallback) { return l->tracks.sample_or(prop, localNow, fallback); };
            const Vec3 anchor{sample(TrackProperty::AnchorX, l->transform.anchor.x),
                              sample(TrackProperty::AnchorY, l->transform.anchor.y),
                              sample(TrackProperty::AnchorZ, l->transform.anchor.z)};
            const TrackProperty orientScale[] = {TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ,
                                                 TrackProperty::ScaleX, TrackProperty::ScaleY, TrackProperty::ScaleZ};
            const f32 oldValues[] = {sample(orientScale[0], l->transform.rotation.x), sample(orientScale[1], l->transform.rotation.y),
                                    sample(orientScale[2], l->transform.rotation.z), sample(orientScale[3], l->transform.scale.x),
                                    sample(orientScale[4], l->transform.scale.y), sample(orientScale[5], l->transform.scale.z)};
            auto animatedTrack = [](Track* t) { return t && t->keys.size() > 1; };
            Track* tx0 = l->tracks.find(TrackProperty::PositionX);
            Track* ty0 = l->tracks.find(TrackProperty::PositionY);
            Track* tz0 = l->tracks.find(TrackProperty::PositionZ);
            struct PosKey { FrameIndex t; Vec3 p; Keyframe style; };
            std::vector<PosKey> posKeys;
            if (animatedTrack(tx0) || animatedTrack(ty0) || animatedTrack(tz0)) {
                const Vec3 base = l->transform.position;
                auto sampleAt = [&](Track* t, FrameIndex k, f32 fb) { return (t && !t->keys.empty()) ? t->sample_keys(k) : fb; };
                for (Track* t : {tx0, ty0, tz0}) {
                    if (!animatedTrack(t)) continue;
                    for (const Keyframe& k : t->keys) {
                        if (std::any_of(posKeys.begin(), posKeys.end(), [&](const PosKey& q) { return q.t == k.time; })) continue;
                        const Vec3 old{sampleAt(tx0, k.time, base.x), sampleAt(ty0, k.time, base.y), sampleAt(tz0, k.time, base.z)};
                        const Vec4 np = M * Vec4{old.x, old.y, old.z, 1};
                        posKeys.push_back({k.time, Vec3{np.x, np.y, np.z}, k});
                    }
                }
            }
            set_local_from(*l, inverse4(newPw) * world, &anchor);
            if (!posKeys.empty()) {
                // Com giro na troca de pai, X animado e Y parado viram os dois
                // animados: todas as trilhas de posição recebem o vetor inteiro,
                // com a curva da chave de origem.
                const bool useZ = now3d || std::any_of(posKeys.begin(), posKeys.end(), [](const PosKey& q) { return std::fabs(q.p.z) > 1e-4f; });
                (void)l->tracks.get_or_create(TrackProperty::PositionX);
                (void)l->tracks.get_or_create(TrackProperty::PositionY);
                if (useZ) (void)l->tracks.get_or_create(TrackProperty::PositionZ);
                // Adding Y or Z may reallocate every track. Acquire references
                // only after all required properties exist.
                Track& X = *l->tracks.find(TrackProperty::PositionX);
                Track& Y = *l->tracks.find(TrackProperty::PositionY);
                Track* Z = useZ ? l->tracks.find(TrackProperty::PositionZ) : nullptr;
                X.clear();
                Y.clear();
                if (Z) Z->clear();
                for (const PosKey& q : posKeys) {
                    auto put = [&](Track& tr, f32 v) {
                        const u32 i = tr.set(q.t, v, q.style.interp);
                        if (i < tr.keys.size()) {
                            Keyframe k = q.style;
                            k.time = q.t;
                            k.value = v;
                            tr.keys[i] = k;
                        }
                    };
                    put(X, q.p.x);
                    put(Y, q.p.y);
                    if (Z) put(*Z, q.p.z);
                }
            }
            const f32 newValues[] = {l->transform.rotation.x, l->transform.rotation.y, l->transform.rotation.z,
                                    l->transform.scale.x, l->transform.scale.y, l->transform.scale.z};
            for (u32 i = 0; i < 6; ++i) {
                if (Track* track = l->tracks.find(orientScale[i]); animatedTrack(track)) {
                    if (i < 3) {
                        const f32 delta = std::remainder(newValues[i] - oldValues[i], 360.f);
                        for (Keyframe& key : track->keys) key.value += delta;
                    } else if (std::fabs(oldValues[i]) > 1e-6f) {
                        const f32 ratio = newValues[i] / oldValues[i];
                        for (Keyframe& key : track->keys) key.value *= ratio;
                    }
                }
            }
            return OkStatus;
        }

        case CommandType::LayerSetBlendMode: {
            Layer* l = need_layer(cmd.layer_blend.layer);
            if (!l) return Errc::NotFound;
            l->blendMode = cmd.layer_blend.mode;
            return OkStatus;
        }

        case CommandType::LayerSetComposition: {
            Layer* l = need_layer(cmd.layer_comp.layer);
            if (!l) return Errc::NotFound;
            const Composition* c = timeline.composition(cmd.layer_comp.comp);
            if (!c) return Errc::NotFound;
            if (comp && !comp->can_nest(*c)) return Errc::InvalidArgument;
            l->nested.composition = cmd.layer_comp.comp;
            l->kind = LayerKind::Composition;
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Transform
        // ---------------------------------------------------------------------
        case CommandType::LayerSetTransform: {
            Layer* l = need_layer(cmd.transform.layer);
            if (!l) return Errc::NotFound;
            l->transform.position = Vec3{cmd.transform.x, cmd.transform.y, cmd.transform.z};
            l->transform.scale    = Vec3{cmd.transform.sx, cmd.transform.sy, cmd.transform.sz};
            l->transform.rotation = Vec3{cmd.transform.rx, cmd.transform.ry, cmd.transform.rz};
            l->transform.anchor   = Vec3{cmd.transform.ax, cmd.transform.ay, cmd.transform.az};
            l->transform.opacity  = clampf(cmd.transform.opacity, 0.0f, 1.0f);
            return OkStatus;
        }
        case CommandType::LayerSetMaterialParam: {
            const auto& c = cmd.material_param;
            Layer* l = need_layer(c.layer);
            if (!l || l->kind != LayerKind::Model3D) return Errc::NotFound;
            if (l->locked) return Errc::InvalidState;
            const auto found = models_.find(l->model.scene.pack());
            if (found == models_.end() || !found->second || c.material >= found->second->materials.size() || c.material >= 4096) return Errc::OutOfRange;
            const auto& source = found->second->materials[c.material];
            MaterialOverride* target = nullptr;
            for (auto& over : l->model.materials) if (over.materialIndex == c.material) { target = &over; break; }
            Track* track = l->tracks.find(TrackProperty::MaterialParam, c.material, c.param);
            if (track && track->has_expression()) return Errc::InvalidState;
            f32 next = c.value;
            if (track && !track->keys.empty()) {
                const auto local = l->local_time(playback_.current());
                track->set(local, c.value);
            }
            if (!target) {
                MaterialOverride over; over.materialIndex = c.material; over.baseColor = source.baseColor; over.metallic = source.metallic; over.roughness = source.roughness;
                l->model.materials.push_back(over); target = &l->model.materials.back();
            }
            target->mask |= 1u << c.param;
            switch (c.param) {
                case 0: target->baseColor.x = next; break; case 1: target->baseColor.y = next; break;
                case 2: target->baseColor.z = next; break; case 3: target->baseColor.w = next; break;
                case 4: target->metallic = next; break; case 5: target->roughness = next; break;
            }
            return OkStatus;
        }
        case CommandType::LayerSetLightParam: {
            Layer* l = need_layer(cmd.shape_param.layer);
            if (!l || l->kind != LayerKind::Light) return Errc::NotFound;
            if (l->locked) return Errc::InvalidState;
            const u32 p = cmd.shape_param.param;
            const f32 v = cmd.shape_param.value;
            if (p > 10 || !std::isfinite(v)) return Errc::InvalidArgument;
            if (p == 10) {
                if (v < 0 || v > 1) return Errc::InvalidArgument;
                l->light.shadowStrength = v;
                return OkStatus;
            }
            if (p == 0) { if (v < 0 || v > 2) return Errc::InvalidArgument; l->light.kind = static_cast<LightKind>(static_cast<u32>(v)); return OkStatus; }
            if (p == 8) { l->light.castShadows = v >= .5f; return OkStatus; }
            f32* field = p == 1 ? &l->light.intensity : p == 2 ? &l->light.color.x : p == 3 ? &l->light.color.y : p == 4 ? &l->light.color.z
                       : p == 5 ? &l->light.range : p == 6 ? &l->light.coneAngle : p == 7 ? &l->light.penumbra : &l->light.shadowBias;
            const f32 maximum = p >= 2 && p <= 4 ? 1.0f : p == 7 ? 1.0f : p == 6 ? 179.0f : p == 9 ? .1f : 1000000.0f;
            if (v < 0 || v > maximum) return Errc::InvalidArgument;
            const auto prop = p >= 1 && p <= 4 ? static_cast<TrackProperty>(static_cast<u32>(TrackProperty::LightIntensity) + p - 1)
                            : p == 6 ? TrackProperty::LightConeAngle : p == 7 ? TrackProperty::LightPenumbra : TrackProperty::_Count;
            Track* track = prop != TrackProperty::_Count ? l->tracks.find(prop) : nullptr;
            if (track && track->has_expression()) return Errc::InvalidState;
            if (track && !track->keys.empty()) track->set(l->local_time(playback_.current()), v);
            else { if (track) track->staticValue = v; *field = v; }
            return OkStatus;
        }
        case CommandType::LayerSetCameraParam: {
            // LENTE (mesma semântica do transform): trilha com keyframe → o
            // keyframe no cabeçote (cria ou atualiza); sem → o valor parado.
            Layer* l = need_layer(cmd.shape_param.layer);
            if (!l || l->kind != LayerKind::Camera) return Errc::NotFound;
            if (l->locked) return Errc::InvalidState;
            const u32 p = cmd.shape_param.param;
            const f32 v = cmd.shape_param.value;
            const FrameIndex local = l->local_time(playback_.current());
            if (p == 1) { l->camera.dofEnabled = v >= 0.5f; return OkStatus; }
            auto keyed = [&](TrackProperty property, f32 value, f32& field) -> Status {
                Track* track = l->tracks.find(property);
                if (track && track->has_expression()) return Status{Errc::InvalidState, "lente controlada por expressao"};
                if (track && !track->keys.empty()) track->set(local, value);
                else field = value;
                return OkStatus;
            };
            if (p == 0) {
                if (v < kCameraFocalMinMm || v > kCameraFocalMaxMm) return Errc::InvalidArgument;
                // A distância focal manda na projeção: FOV = 2·atan(24 / 2f).
                // Trilha de mm com keyframe → keyframe; FOV animado (projeto
                // antigo, script) → keyframe de FOV equivalente; o parado
                // (fov + mm) acompanha sempre.
                Track* focal = l->tracks.find(TrackProperty::FocalLength);
                Track* fov = l->tracks.find(TrackProperty::Fov);
                const bool focalKeyed = focal && !focal->keys.empty();
                if ((focal && focal->has_expression()) || (!focalKeyed && fov && fov->has_expression()))
                    return Status{Errc::InvalidState, "lente controlada por expressao"};
                if (focalKeyed) { focal->set(local, v); return OkStatus; }
                if (fov && !fov->keys.empty()) fov->set(local, camera_fov_from_focal(v));
                l->camera.focalLength = v;
                l->camera.fov = camera_fov_from_focal(v);
                return OkStatus;
            }
            if (p == 2) {
                if (v < 0.01f || v > 1.0e7f) return Errc::InvalidArgument;
                return keyed(TrackProperty::FocusDistance, v, l->camera.focusDistance);
            }
            if (p == 3) {
                if (v < 0.5f || v > 64.0f) return Errc::InvalidArgument;
                return keyed(TrackProperty::Aperture, v, l->camera.aperture);
            }
            if (v < 0.0f || v > 4.0f) return Errc::InvalidArgument;
            return keyed(TrackProperty::CameraBlur, v, l->camera.blurAmount);
        }
        case CommandType::LayerLayoutTransform: {
            Layer* l = need_layer(cmd.shape_param.layer);
            if (!l) return Errc::NotFound;
            if (l->locked) return Errc::InvalidState;
            const u32 property = cmd.shape_param.param, axis = property % 3;
            f32* scalar = property == 12 ? &l->transform.opacity : property == 13 ? &l->transform.skewX
                         : property == 14 ? &l->transform.skewY : nullptr;
            Vec3* vector = property < 3 ? &l->transform.position : property < 6 ? &l->transform.scale
                         : property < 9 ? &l->transform.rotation : &l->transform.anchor;
            const f32 base = scalar ? *scalar : axis == 0 ? vector->x : axis == 1 ? vector->y : vector->z;
            Track* track = l->tracks.find(static_cast<TrackProperty>(property));
            if (track && track->has_expression()) return Status{Errc::InvalidState, "layout transform controlled by expression"};
            const f32 current = track ? track->value_or(l->local_time(playback_.current()), base) : base;
            const f64 target = cmd.shape_param.value;
            // At zero scale, multiplication cannot restore a visible object:
            // translate the whole scale curve instead, preserving its deltas.
            const bool multiply = property >= 3 && property < 6 && std::fabs(current) > 1e-6f;
            const f64 gain = multiply ? target / current : 1.0;
            const f64 delta = multiply ? 0.0 : target - current;
            auto mapped = [&](f32 value) { return static_cast<f64>(value) * gain + delta; };
            auto fits = [](f64 value) { return std::isfinite(value) && std::fabs(value) <= std::numeric_limits<f32>::max(); };
            if (!fits(mapped(base))) return Errc::InvalidArgument;
            if (track) {
                if (!fits(mapped(track->staticValue))) return Errc::InvalidArgument;
                for (const auto& key : track->keys)
                    if (!fits(mapped(key.value)) || !fits(key.tangentIn * gain) || !fits(key.tangentOut * gain)) return Errc::InvalidArgument;
            }
            const f32 next = static_cast<f32>(mapped(base));
            if (scalar) *scalar = next;
            else if (axis == 0) vector->x = next; else if (axis == 1) vector->y = next; else vector->z = next;
            if (track) {
                track->staticValue = static_cast<f32>(mapped(track->staticValue));
                for (auto& key : track->keys) {
                    key.value = static_cast<f32>(mapped(key.value));
                    key.tangentIn = static_cast<f32>(key.tangentIn * gain);
                    key.tangentOut = static_cast<f32>(key.tangentOut * gain);
                }
            }
            return OkStatus;
        }
        case CommandType::LayerSetPosition: {
            Layer* l = need_layer(cmd.position.layer);
            if (!l) return Errc::NotFound;
            l->transform.position = Vec3{cmd.position.x, cmd.position.y, cmd.position.z};
            return OkStatus;
        }
        case CommandType::LayerSetScale: {
            Layer* l = need_layer(cmd.scale.layer);
            if (!l) return Errc::NotFound;
            l->transform.scale = Vec3{cmd.scale.sx, cmd.scale.sy, cmd.scale.sz};
            return OkStatus;
        }
        case CommandType::LayerSetRotation: {
            Layer* l = need_layer(cmd.rotation.layer);
            if (!l) return Errc::NotFound;
            l->transform.rotation = Vec3{cmd.rotation.rx, cmd.rotation.ry, cmd.rotation.rz};
            return OkStatus;
        }
        case CommandType::LayerSetAnchor: {
            Layer* l = need_layer(cmd.anchor.layer);
            if (!l) return Errc::NotFound;
            l->transform.anchor = Vec3{cmd.anchor.ax, cmd.anchor.ay, cmd.anchor.az};
            return OkStatus;
        }
        case CommandType::LayerSetOpacity: {
            Layer* l = need_layer(cmd.opacity.layer);
            if (!l) return Errc::NotFound;
            l->transform.opacity = clampf(cmd.opacity.opacity, 0.0f, 1.0f);
            return OkStatus;
        }
        case CommandType::LayerSetSkew: {
            Layer* l = need_layer(cmd.skew.layer);
            if (!l) return Errc::NotFound;
            l->transform.skewX = cmd.skew.skewX;
            l->transform.skewY = cmd.skew.skewY;
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Keyframes. Para parâmetro de efeito, `effectIndex` é o ID do efeito
        // (estável ao reordenar) e `effectParamIndex` é param*4 + componente.
        // ---------------------------------------------------------------------
        case CommandType::KeyframeInsert: {
            Layer* l = need_layer(cmd.keyframe.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = command_track(*l, cmd.keyframe.track, true);
            const f32 scale = is_remap_time_alias(*l, cmd.keyframe.track) ? static_cast<f32>(comp->fps()) : 1.0f;
            // Na curva de tempo, regravar uma chave que já existe mantém a
            // facilidade escolhida nela (arrastar o Tempo não desfaz o "Suave").
            if (track == &l->timeRemap && track->find_exact(cmd.keyframe.time) != kInvalidIndex) {
                track->keys[track->find_exact(cmd.keyframe.time)].value = cmd.keyframe.value * scale;
                return OkStatus;
            }
            if (track->property == TrackProperty::Speed) {
                // Velocidade: presa em [0, 16]; regravar mantém a facilidade.
                const f32 v = clampf(cmd.keyframe.value, 0.0f, 16.0f);
                if (const u32 at = track->find_exact(cmd.keyframe.time); at != kInvalidIndex) track->keys[at].value = v;
                else (void)track->set(cmd.keyframe.time, v, Interpolation::Linear);
                return OkStatus;
            }
            (void)track->set(cmd.keyframe.time, cmd.keyframe.value * scale, Interpolation::Linear);
            return OkStatus;
        }
        case CommandType::KeyframeDelete: {
            Layer* l = need_layer(cmd.keyframe.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = command_track(*l, cmd.keyframe.track, false);
            if (!track || !track->remove(cmd.keyframe.time)) return Errc::NotFound;
            return OkStatus;
        }
        case CommandType::KeyframeMove: {
            Layer* l = need_layer(cmd.keyframe_move.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = command_track(*l, cmd.keyframe_move.track, false);
            if (!track) return Errc::NotFound;
            if (track->move(cmd.keyframe_move.fromTime, cmd.keyframe_move.toTime) == kInvalidIndex) return Errc::NotFound;
            return OkStatus;
        }
        case CommandType::KeyframeSetValue: {
            Layer* l = need_layer(cmd.keyframe.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = command_track(*l, cmd.keyframe.track, false);
            if (!track) return Errc::NotFound;
            const u32 idx = track->find_exact(cmd.keyframe.time);
            if (idx == kInvalidIndex) return Errc::NotFound;
            track->keys[idx].value = cmd.keyframe.value * (is_remap_time_alias(*l, cmd.keyframe.track) ? static_cast<f32>(comp->fps()) : 1.0f);
            if (track->property == TrackProperty::Speed) track->keys[idx].value = clampf(track->keys[idx].value, 0.0f, 16.0f);
            return OkStatus;
        }
        case CommandType::KeyframeSetInterpolation:
        case CommandType::KeyframeSetBezier:
        case CommandType::KeyframeSetEasing: {
            Layer* l = need_layer(cmd.keyframe_interp.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = command_track(*l, cmd.keyframe_interp.track, false);
            if (!track) return Errc::NotFound;
            track->set_interpolation(cmd.keyframe_interp.time, cmd.keyframe_interp.interp,
                                     cmd.keyframe_interp.bx1, cmd.keyframe_interp.by1,
                                     cmd.keyframe_interp.bx2, cmd.keyframe_interp.by2,
                                     cmd.keyframe_interp.power);
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Máscaras (dados do modelo; a rasterização é do Renderer — MaskRaster)
        // ---------------------------------------------------------------------
        case CommandType::MaskCreate: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            if (l->masks.size() >= kMaxMaskCount) return Errc::OutOfRange;
            Mask m;
            m.id = l->alloc_mask_id();
            m.name = stringData ? std::string(stringData) : ("Mascara " + std::to_string(m.id + 1));
            l->masks.push_back(std::move(m));
            return OkStatus;
        }
        case CommandType::MaskDelete: {
            Layer* l = need_layer(cmd.mask_scalar.layer);
            if (!l) return Errc::NotFound;
            for (auto it = l->masks.begin(); it != l->masks.end(); ++it) {
                if (it->id == cmd.mask_scalar.mask.index) {
                    l->tracks.remove_if([&](const Track& tr) { return tr.property == TrackProperty::MaskParam && tr.effectIndex == it->id; });
                    l->masks.erase(it); return OkStatus;
                }
            }
            return Errc::NotFound;
        }
        case CommandType::MaskSetOperation: {
            Layer* l = need_layer(cmd.mask_op.layer);
            if (!l) return Errc::NotFound;
            Mask* m = l->find_mask(cmd.mask_op.mask);
            if (!m) return Errc::NotFound;
            m->operation = cmd.mask_op.op;
            m->cacheKey = 0;
            return OkStatus;
        }
        case CommandType::MaskSetFeather:
        case CommandType::MaskSetExpansion:
        case CommandType::MaskSetOpacity: {
            Layer* l = need_layer(cmd.mask_scalar.layer);
            if (!l) return Errc::NotFound;
            Mask* m = l->find_mask(cmd.mask_scalar.mask);
            if (!m) return Errc::NotFound;
            m->cacheKey = 0;
            const u32 param = cmd.type == CommandType::MaskSetFeather ? 0 : cmd.type == CommandType::MaskSetExpansion ? 1 : 2;
            write_mask_param(*l, *m, param, cmd.mask_scalar.value, playback_.current());
            return OkStatus;
        }
        case CommandType::MaskSetPath: {
            Layer* l = need_layer(cmd.mask_point.layer);
            if (!l) return Errc::NotFound;
            Mask* m = l->find_mask(cmd.mask_point.mask);
            if (!m) return Errc::NotFound;
            const u32 idx = cmd.mask_point.pointIndex;
            if (idx >= m->points.size()) return Errc::OutOfRange;
            MaskPoint& p = m->points[idx];
            p.position   = Vec2{cmd.mask_point.x, cmd.mask_point.y};
            p.inTangent  = Vec2{cmd.mask_point.inX, cmd.mask_point.inY};
            p.outTangent = Vec2{cmd.mask_point.outX, cmd.mask_point.outY};
            m->cacheKey = 0;
            return OkStatus;
        }
        case CommandType::MaskSetPathCommit: {
            Layer* l = need_layer(cmd.mask_commit.layer);
            if (!l) return Errc::NotFound;
            Mask* m = l->find_mask(cmd.mask_commit.mask);
            if (!m) return Errc::NotFound;
            if (cmd.mask_commit.pointCount > 100000u) return Errc::OutOfRange;
            // Aplica o número de pontos desenhado (os novos entram na origem e o
            // MaskSetPath de cada um os coloca no lugar).
            m->points.resize(cmd.mask_commit.pointCount);
            m->closed = cmd.mask_commit.closed;
            m->cacheKey = 0;
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Efeitos
        // ---------------------------------------------------------------------
        case CommandType::EffectAdd: {
            Layer* l = need_layer(cmd.effect_add.layer);
            if (!l) return Errc::NotFound;
            if (l->effects.size() >= kMaxEffectCount) return Errc::OutOfRange;
            const EffectTypeId type = cmd.effect_add.effectType;
            if (type == effect_type_id(effect_keys::kRotobrush) && l->kind != LayerKind::Image && l->kind != LayerKind::Video) return Errc::InvalidArgument;
            if ((type == effect_type_id(text::kTransformEffect) || type == effect_type_id(text::kAnimatorEffect)) && l->kind != LayerKind::Text) {
                const Asset* source = project_->asset(l->model.scene);
                scene3d::Text3DSpec spec;
                if (l->kind != LayerKind::Model3D || !source || !scene3d::decode_text3d(source->sourcePath, spec)) return Errc::InvalidArgument;
                const Status ready = ensure_text3d_layout(*l);
                if (!ready.ok()) return ready;
            }
            const ParameterRegistry* params = effectRegistry_.params(type);
            if (!params) return Errc::NotSupported;
            if (type == effect_type_id(effect_keys::kText3DLayout)) {
                const Status ready = ensure_text3d_layout(*l);
                if (!ready.ok()) return ready;
            }
            EffectInstance e;
            e.id = l->alloc_effect_id();
            e.type = type;
            initialize_instance(e, *params);
            if (cmd.effect_add.index < l->effects.size()) {
                l->effects.insert(l->effects.begin() + cmd.effect_add.index, std::move(e));
            } else {
                l->effects.push_back(std::move(e));
            }
            // Remapear tempo é declarativo: quem age é a curva da camada. Pôr o
            // efeito liga a curva (a rampa equivalente ao tempo de agora), como
            // ligar o remapeamento no painel de velocidade.
            if (is_time_remap_type(type)) enable_time_remap_curve(*l);
            return OkStatus;
        }
        case CommandType::EffectRemove: {
            Layer* l = need_layer(cmd.effect_ref.layer);
            if (!l) return Errc::NotFound;
            const u32 idx = l->effect_index(cmd.effect_ref.effect);
            if (idx == kInvalidIndex) return Errc::NotFound;
            const u32 id = l->effects[idx].id;
            const bool era_remap = is_time_remap_type(l->effects[idx].type);
            l->effects.erase(l->effects.begin() + idx);
            // Os keyframes do efeito saem junto: órfãos ficariam no arquivo e
            // voltariam a animar um efeito novo que reusasse o id.
            for (u32 t = 0; t < l->tracks.size(); ++t) {
                Track& tr = l->tracks.at(t);
                if (tr.property == TrackProperty::EffectParam && tr.effectIndex == id) tr.clear();
            }
            // Tirar o Remapear tempo desliga a curva — mas ela FICA guardada,
            // igual a desligar o remapeamento pelo painel de velocidade.
            if (era_remap) l->timeRemapEnabled = std::any_of(l->effects.begin(), l->effects.end(), [](const EffectInstance& item) {
                return item.enabled && is_time_remap_type(item.type);
            });
            return OkStatus;
        }
        case CommandType::EffectReorder: {
            Layer* l = need_layer(cmd.effect_reorder.layer);
            if (!l) return Errc::NotFound;
            const u32 from = l->effect_index(cmd.effect_reorder.effect);
            const u32 to = cmd.effect_reorder.newIndex;
            if (from == kInvalidIndex || to >= l->effects.size()) return Errc::OutOfRange;
            EffectInstance e = std::move(l->effects[from]);
            l->effects.erase(l->effects.begin() + from);
            l->effects.insert(l->effects.begin() + to, std::move(e));
            return OkStatus;
        }
        case CommandType::EffectSetEnabled: {
            Layer* l = need_layer(cmd.effect_enabled.layer);
            if (!l) return Errc::NotFound;
            EffectInstance* e = l->find_effect(cmd.effect_enabled.effect);
            if (!e) return Errc::NotFound;
            e->enabled = cmd.effect_enabled.enabled;
            if (is_time_remap_type(e->type)) {
                const bool on = std::any_of(l->effects.begin(), l->effects.end(), [](const EffectInstance& item) {
                    return item.enabled && is_time_remap_type(item.type);
                });
                if (on) enable_time_remap_curve(*l); else l->timeRemapEnabled = false;
            }
            return OkStatus;
        }
        case CommandType::EffectSetParam: {
            Layer* l = need_layer(cmd.effect_param.layer);
            if (!l) return Errc::NotFound;
            EffectInstance* e = l->find_effect(cmd.effect_param.effect);
            if (!e) return Errc::NotFound;
            const u32 p = cmd.effect_param.paramIndex;
            if (p >= e->params.size()) return Errc::OutOfRange;
            // Remapear tempo: "Tempo" e "Interpolação do tempo" SÃO a curva da
            // camada (o mesmo dado que o gráfico do painel de velocidade edita),
            // em segundos da fonte como no After Effects.
            if (is_time_remap_type(e->type)) {
                const f64 compFps = comp && comp->fps() > 0.0 ? comp->fps() : 30.0;
                const FrameIndex local = l->local_time(playback_.current());
                enable_time_remap_curve(*l);
                const u32 at = l->timeRemap.find_exact(local);
                if (p == 0) {
                    // "Tempo" em segundos da fonte; a curva guarda quadros,
                    // presos à fonte (o dedo pode passar do fim da régua).
                    const f64 last = project_ ? source_last_frame(*project_, *l, compFps) : 0.0;
                    f64 want = std::isfinite(cmd.effect_param.value) ? cmd.effect_param.value * compFps : 0.0;
                    want = last > 0.0 ? std::clamp(want, 0.0, last) : std::max(0.0, want);
                    const f32 frames = static_cast<f32>(want);
                    // A chave nova nasce linear; a que já existe mantém a sua.
                    const Interpolation interp = at == kInvalidIndex ? Interpolation::Linear : l->timeRemap.keys[at].interp;
                    write_time_remap(*l, local, frames, interp);
                    e->params[0].constant.v[0] = cmd.effect_param.value;
                    return OkStatus;
                }
                if (p == 1) {
                    // Facilidade da chave no cabeçote (cria a chave se não houver).
                    const Interpolation mode = remap_interp_of_mode(cmd.effect_param.value);
                    const f32 value = l->timeRemap.sample_keys(local);
                    write_time_remap(*l, local, value, mode);
                    e->params[1].constant.v[0] = cmd.effect_param.value;
                    return OkStatus;
                }
            }
            // Referência a camada ("Camada de áudio"): o valor que chega é o
            // ÍNDICE da camada (a parte baixa do LayerId; −1 = nenhuma) — um
            // float carrega o índice exato, não o id inteiro com a geração.
            if (const ParameterRegistry* specs = effectRegistry_.params(e->type);
                specs && p < specs->count() && specs->at(p).type == ParamType::LayerReference) {
                u64 ref = 0;
                const f32 v = cmd.effect_param.value;
                if (comp && std::isfinite(v) && v >= 0.0f) {
                    const u32 index = static_cast<u32>(v + 0.5f);
                    const OrderedIds<LayerId>& order = comp->order();
                    for (u32 k = 0; k < order.size(); ++k) {
                        if (order.at(k).index == index) { ref = order.at(k).pack(); break; }
                    }
                }
                e->params[p].constant.ref = ref;
                e->params[p].constant.v[0] = ref ? static_cast<f32>(LayerId::unpack(ref).index) : -1.0f;
                return OkStatus;
            }
            e->params[p].constant.v[0] = cmd.effect_param.value;
            return OkStatus;
        }
        case CommandType::EffectSetColorParam: {
            // Valor de até 4 componentes: cor, ponto 2D/3D.
            Layer* l = need_layer(cmd.effect_color.layer);
            if (!l) return Errc::NotFound;
            EffectInstance* e = l->find_effect(cmd.effect_color.effect);
            if (!e) return Errc::NotFound;
            const u32 p = cmd.effect_color.paramIndex;
            if (p >= e->params.size()) return Errc::OutOfRange;
            ParamValue& v = e->params[p].constant;
            v.v[0] = cmd.effect_color.r;
            v.v[1] = cmd.effect_color.g;
            v.v[2] = cmd.effect_color.b;
            v.v[3] = cmd.effect_color.a;
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Áudio
        // ---------------------------------------------------------------------
        case CommandType::AudioSetGain: {
            Layer* l = need_layer(cmd.audio_gain.layer);
            if (!l) return Errc::NotFound;
            l->gain = clampf(cmd.audio_gain.gain, 0.0f, 4.0f);
            return OkStatus;
        }
        case CommandType::LayerSetSpeed: {
            Layer* l = need_layer(cmd.audio_gain.layer);
            if (!l || !comp) return Errc::NotFound;
            const f32 s = clampf(cmd.audio_gain.gain, 0.05f, 16.0f);
            // O TRECHO da fonte fica o mesmo; a duração na timeline acompanha
            // (2× mais rápido = metade do tempo), como em todo editor.
            const f64 span = static_cast<f64>(l->end.value - l->start.value) * (l->speed > 0.0f ? l->speed : 1.0f);
            const i64 frames = std::max<i64>(1, static_cast<i64>(std::llround(span / s)));
            l->speed = s;
            l->end = FrameIndex{l->start.value + frames};
            if (l->end.value > comp->duration().value) {
        comp->set_duration(l->end);
        playback_.configure(comp->fps(), comp->duration());
    }
            playback_.configure(comp->fps(), comp->duration());
            return OkStatus;
        }
        case CommandType::ShapeSetFill:
        case CommandType::ShapeSetStroke: {
            Layer* l = need_layer(cmd.text_color.layer);
            if (!l || l->kind != LayerKind::Shape) return Errc::NotFound;
            const Vec4 c{clampf(cmd.text_color.r, 0, 1), clampf(cmd.text_color.g, 0, 1), clampf(cmd.text_color.b, 0, 1),
                         clampf(cmd.text_color.a, 0, 1)};
            if (cmd.type == CommandType::ShapeSetFill) {
                l->shape.fillColor = c;
                l->shape.filled = c.w > 0.0f;
            } else {
                l->shape.strokeColor = c;
            }
            return OkStatus;
        }
        case CommandType::ShapeSetParam: {
            Layer* l = need_layer(cmd.shape_param.layer);
            if (!l || l->kind != LayerKind::Shape) return Errc::NotFound;
            const f32 v = cmd.shape_param.value;
            ShapeData& sh = l->shape;
            switch (cmd.shape_param.param) {
                case 0: {
                    // 0..10 e 12..15 são SDF; 11 é a camada vetorial (não se troca por aqui).
                    const u32 t = static_cast<u32>(std::clamp(v, 0.0f, 15.0f));
                    sh.shapeType = t == kShapeVector ? 10u : t;
                    break;
                }
                case 1: sh.cornerRadius = std::max(0.0f, v); break;
                case 2: sh.points = std::clamp(std::round(v), 3.0f, 64.0f); break;
                case 3: sh.innerRadius = clampf(v, 0.05f, 0.95f); break;
                case 4: sh.strokeWidth = std::clamp(v, 0.0f, 500.0f); break;
                case 5:
                case 6: {
                    // Tamanho muda em volta do centro: a âncora acompanha a metade.
                    const f32 nv = std::clamp(v, 1.0f, 16384.0f);
                    if (cmd.shape_param.param == 5) { sh.bounds.w = nv; l->transform.anchor.x = nv * 0.5f; }
                    else { sh.bounds.h = nv; l->transform.anchor.y = nv * 0.5f; }
                    break;
                }
                default: return Errc::InvalidArgument;
            }
            return OkStatus;
        }
        case CommandType::LayerSetReversed: {
            Layer* l = need_layer(cmd.audio_flag.layer);
            if (!l) return Errc::NotFound;
            l->reversed = cmd.audio_flag.flag;
            return OkStatus;
        }
        case CommandType::AudioSetVolume: {
            Layer* l = need_layer(cmd.audio_gain.layer);
            if (!l) return Errc::NotFound;
            l->tracks.set_static(TrackProperty::AudioVolume, clampf(cmd.audio_gain.gain, 0.0f, 2.0f));
            return OkStatus;
        }
        case CommandType::AudioSetPan: {
            Layer* l = need_layer(cmd.audio_gain.layer);
            if (!l) return Errc::NotFound;
            l->pan = clampf(cmd.audio_gain.gain, -1.0f, 1.0f);
            return OkStatus;
        }
        case CommandType::AudioSetMuted: {
            Layer* l = need_layer(cmd.audio_flag.layer);
            if (!l) return Errc::NotFound;
            l->muted = cmd.audio_flag.flag;
            return OkStatus;
        }
        case CommandType::AudioSetSolo: {
            Layer* l = need_layer(cmd.audio_flag.layer);
            if (!l) return Errc::NotFound;
            // Solo é avaliado na mixagem (audio::build_snapshot), não gravado
            // como "mudo" nas outras: sair do solo devolve cada uma ao estado
            // que o usuário deixou.
            l->solo = cmd.audio_flag.flag;
            return OkStatus;
        }
        case CommandType::AudioSetFadeIn: {
            Layer* l = need_layer(cmd.audio_fade.layer);
            if (!l) return Errc::NotFound;
            l->fadeIn = cmd.audio_fade.duration;
            return OkStatus;
        }
        case CommandType::AudioSetFadeOut: {
            Layer* l = need_layer(cmd.audio_fade.layer);
            if (!l) return Errc::NotFound;
            l->fadeOut = cmd.audio_fade.duration;
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Texto (modelo; o rasterizador de texto é fase futura)
        // ---------------------------------------------------------------------
        case CommandType::TextSetContent: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            if (stringData) l->text.content = stringData;
            recenter_text(*l);
            return OkStatus;
        }
        case CommandType::TextSetFont:
            return Status{Errc::NotImplemented, "troca de fonte ainda nao implementada"};
        case CommandType::TextSetSize: {
            Layer* l = need_layer(cmd.text_size.layer);
            if (!l) return Errc::NotFound;
            l->text.size = std::clamp(cmd.text_size.size, 1.0f, 2000.0f);
            recenter_text(*l);
            return OkStatus;
        }
        case CommandType::TextSetColor: {
            Layer* l = need_layer(cmd.text_color.layer);
            if (!l) return Errc::NotFound;
            l->text.color = Vec4{cmd.text_color.r, cmd.text_color.g, cmd.text_color.b, cmd.text_color.a};
            return OkStatus;
        }
        case CommandType::TextSetAlignment: {
            Layer* l = need_layer(cmd.text_align.layer);
            if (!l) return Errc::NotFound;
            l->text.alignment = std::min(cmd.text_align.alignment, 2u);
            recenter_text(*l);
            return OkStatus;
        }
        case CommandType::TextSetStrokeWidth: {
            Layer* l = need_layer(cmd.text_stroke_width.layer);
            if (!l) return Errc::NotFound;
            l->text.strokeWidth = std::clamp(cmd.text_stroke_width.width, 0.0f, 200.0f);
            recenter_text(*l);
            return OkStatus;
        }
        case CommandType::TextSetStrokeColor: {
            Layer* l = need_layer(cmd.text_color.layer);
            if (!l) return Errc::NotFound;
            l->text.strokeColor = Vec4{cmd.text_color.r, cmd.text_color.g, cmd.text_color.b, cmd.text_color.a};
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Composição
        // ---------------------------------------------------------------------
        case CommandType::CompositionCreate: {
            const CompositionId id = timeline.create_composition(
                stringData ? std::string(stringData) : std::string("Composicao"), 1920, 1080, 30.0);
            if (!id.valid()) return Errc::OutOfMemory;
            return OkStatus;
        }
        case CommandType::CompositionDelete: {
            if (!timeline.remove_composition(cmd.comp_ref.comp)) return Errc::InvalidState;
            return OkStatus;
        }
        case CommandType::CompositionSetSize: {
            Composition* c = timeline.composition(cmd.comp_size.comp);
            if (!c) return Errc::NotFound;
            if (cmd.comp_size.width == 0 || cmd.comp_size.height == 0) return Errc::InvalidArgument;
            // Teto como lado maior × lado menor: uma composição em pé com o
            // mesmo número de pixels que a deitada também vale.
            const u32 capLong = std::max(caps_.max_export_width(), caps_.max_export_height());
            const u32 capShort = std::min(caps_.max_export_width(), caps_.max_export_height());
            if (std::max(cmd.comp_size.width, cmd.comp_size.height) > capLong
                || std::min(cmd.comp_size.width, cmd.comp_size.height) > capShort) {
                return Errc::NotSupported;
            }
            c->resize_content(cmd.comp_size.width, cmd.comp_size.height);
            if (c == comp) adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
            return OkStatus;
        }
        case CommandType::CompositionSetFps: {
            Composition* c = timeline.composition(cmd.comp_fps.comp);
            if (!c) return Errc::NotFound;
            if (cmd.comp_fps.fps <= 0.0 || cmd.comp_fps.fps > 240.0) return Errc::InvalidArgument;
            const f64 k = cmd.comp_fps.fps / c->fps();
            c->retime(cmd.comp_fps.fps);
            if (c == comp) {
                timeline.clock().set_fps(cmd.comp_fps.fps);
                // O cabeçote fica no mesmo SEGUNDO.
                const FrameIndex at{static_cast<i64>(std::llround(static_cast<f64>(playback_.current().value) * k))};
                playback_.configure(c->fps(), c->duration());
                playback_.seek(at, now);
                sync_timeline();
                adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
            }
            return OkStatus;
        }
        case CommandType::CompositionSetDuration: {
            Composition* c = timeline.composition(cmd.comp_duration.comp);
            if (!c) return Errc::NotFound;
            c->set_duration(cmd.comp_duration.duration);
            if (c == comp) {
                playback_.configure(c->fps(), c->duration());
                sync_timeline();
            }
            return OkStatus;
        }
        case CommandType::CompositionSetBackground: {
            Composition* c = timeline.composition(cmd.comp_background.comp);
            if (!c) return Errc::NotFound;
            c->set_background(Color{cmd.comp_background.r, cmd.comp_background.g,
                                    cmd.comp_background.b, cmd.comp_background.a});
            return OkStatus;
        }
        case CommandType::ProjectSetCurrentComposition: {
            if (!timeline.set_current(cmd.comp_ref.comp)) return Errc::NotFound;
            if (Composition* c = timeline.composition(cmd.comp_ref.comp)) {
                adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
            }
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Visualização
        // ---------------------------------------------------------------------
        case CommandType::ViewportSetZoom:
            project_->editor_settings().viewportZoom = clampf(cmd.viewport_zoom.zoom, 0.01f, 64.0f);
            return OkStatus;
        case CommandType::ViewportSetPan:
            project_->editor_settings().viewportPan = Vec2{cmd.viewport_pan.x, cmd.viewport_pan.y};
            return OkStatus;
        case CommandType::ViewportSetPreviewScale: {
            if (cmd.preview_scale.automatic) {
                adapt().set_user_scale(PreviewScale::Auto);
                return OkStatus;
            }
            const u32 num = cmd.preview_scale.scaleNumerator;
            const u32 den = cmd.preview_scale.scaleDenominator;
            if (num == 0 || den == 0) return Errc::InvalidArgument;
            PreviewScale scale = PreviewScale::Full;
            if (den >= 8) scale = PreviewScale::Eighth;
            else if (den >= 4) scale = PreviewScale::Quarter;
            else if (den >= 2) scale = PreviewScale::Half;
            adapt().set_user_scale(scale);
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Reprodução — tudo passa pelo PlaybackController.
        // ---------------------------------------------------------------------
        case CommandType::PlaybackPlay:
            if (comp) playback_.configure(comp->fps(), comp->duration());
            playback_.play(now);
            sync_timeline();
            return OkStatus;
        case CommandType::PlaybackPause:
            playback_.pause(now);
            sync_timeline();
            return OkStatus;
        case CommandType::PlaybackToggle:
            if (comp) playback_.configure(comp->fps(), comp->duration());
            playback_.toggle(now);
            sync_timeline();
            return OkStatus;
        case CommandType::PlaybackSeek: {
            if (cmd.seek.time.value < 0) return Errc::InvalidArgument;
            const f64 fps = comp ? comp->fps() : 30.0;
            if (comp) playback_.configure(comp->fps(), comp->duration());
            playback_.seek(frame_at(cmd.seek.time, fps), now);
            sync_timeline();
            return OkStatus;
        }
        case CommandType::PlaybackScrubBegin:
            playback_.begin_scrub(now);
            sync_timeline();
            return OkStatus;
        case CommandType::PlaybackScrub: {
            if (cmd.seek.time.value < 0) return Errc::InvalidArgument;
            const f64 fps = comp ? comp->fps() : 30.0;
            if (comp) playback_.configure(comp->fps(), comp->duration());
            playback_.scrub(frame_at(cmd.seek.time, fps), now);
            sync_timeline();
            return OkStatus;
        }
        case CommandType::PlaybackScrubEnd:
            playback_.end_scrub(now);
            sync_timeline();
            return OkStatus;
        case CommandType::PlaybackStep:
            if (comp) playback_.configure(comp->fps(), comp->duration());
            playback_.step(cmd.step.frames, now);
            sync_timeline();
            return OkStatus;
        case CommandType::PlaybackSetLoop:
            playback_.set_loop(cmd.loop.loop);
            timeline.set_loop(cmd.loop.loop);
            return OkStatus;
        case CommandType::PlaybackSetSpeed: {
            if (cmd.speed.speed <= 0.0f || cmd.speed.speed > 16.0f) return Errc::InvalidArgument;
            playback_.set_speed(cmd.speed.speed, now);
            timeline.set_speed(cmd.speed.speed);
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Histórico
        // ---------------------------------------------------------------------
        case CommandType::UndoBeginGroup:
            history_.begin_group(stringData ? stringData : "acao");
            return OkStatus;
        case CommandType::UndoEndGroup:
            history_.end_group();
            return OkStatus;
        case CommandType::Undo:
            if (!history_.undo(timeline)) return Errc::InvalidState;
            after_history_restore_locked();
            return OkStatus;
        case CommandType::Redo:
            if (!history_.redo(timeline)) return Errc::InvalidState;
            after_history_restore_locked();
            return OkStatus;

        // ---------------------------------------------------------------------
        // Export
        // ---------------------------------------------------------------------
        case CommandType::ExportRequest:
            // O export tem thread e sink próprios: entra por Engine::start_export,
            // não pela fila (que roda com o modelo travado).
            return Status{Errc::NotSupported, "use start_export"};
        case CommandType::ExportCancel:
            return cancel_export();

        // ---------------------------------------------------------------------
        // 3D — superfície de comandos pronta; a cena é a próxima fase.
        // ---------------------------------------------------------------------
        case CommandType::SceneLoadModel:
        case CommandType::SceneSetCamera:
        case CommandType::SceneAddLight:
        case CommandType::SceneSetLightParam:
        case CommandType::SceneSetModelTransform:
        case CommandType::SceneSetAnimationClip:
        case CommandType::SceneSetMaterialParam:
        case CommandType::SceneSetEnvironment:
            return Status{Errc::NotImplemented, "cena 3D ainda nao implementada"};

        case CommandType::Nop:
        default:
            return OkStatus;
    }
}

// =============================================================================
// Structs da fronteira — campo a campo, de propósito (ver BridgePods.hpp).
// =============================================================================
void Engine::fill_status(bridge::EngineStatusPOD& out) noexcept {
    const EngineStatus st = read_status();
    out = bridge::EngineStatusPOD{};
    out.state = static_cast<i32>(st.state);
    out.lastError = static_cast<i32>(st.lastError);
    std::snprintf(out.errorDetail, sizeof(out.errorDetail), "%s", st.lastErrorDetail);
    out.currentFps = st.currentFps;
    out.averageFrameMs = st.averageFrameMs;
    out.gpuMs = st.gpuMs;
    out.cpuMs = st.cpuMs;
    out.decodeMs = st.decodeMs;
    out.cacheHitRate = st.cacheHitRate;
    out.memoryPressure = st.memoryPressure;
    out.previewWidth = st.previewWidth;
    out.previewHeight = st.previewHeight;
    out.previewNumerator = st.previewNumerator;
    out.previewDenominator = st.previewDenominator;
    out.previewAuto = st.previewAuto ? 1u : 0u;
    out.playhead = st.playhead.value;
    out.duration = st.duration.value;
    out.playing = st.playing ? 1u : 0u;
    out.compFps = static_cast<f32>(st.compFps);
    out.compWidth = st.compWidth;
    out.compHeight = st.compHeight;
    out.thumbnailGeneration = thumbs_.generation() + (waveforms_ ? waveforms_->generation() : 0u);
    out.modelRevision = modelRevision_.load(std::memory_order_acquire);
    out.layerCount = st.layerCount;
    out.selectedCount = st.selectedCount;
    out.canUndo = st.canUndo ? 1u : 0u;
    out.canRedo = st.canRedo ? 1u : 0u;
    out.undoDepth = st.undoDepth;
    out.assetCount = st.assetCount;
    out.dirty = st.dirty ? 1u : 0u;
    out.recoveryAvailable = st.recoveryAvailable ? 1u : 0u;
    out.droppedFrames = st.droppedFrames;
    out.passesExecuted = st.passesExecuted;
    out.passesCulled = st.passesCulled;
    out.gpuMemoryBytes = st.gpuMemoryBytes;
    out.cpuMemoryBytes = st.cpuMemoryBytes;
}

void Engine::fill_telemetry(bridge::TelemetryPOD& out) noexcept {
    const EngineTelemetry t = read_telemetry();
    out = bridge::TelemetryPOD{};
    out.frameMs = t.frame.cpuMs;
    out.gpuMs = t.frame.gpuMs;
    out.cpuMs = t.frame.cpuMs;
    out.decodeMs = t.frame.decodeMs;
    out.frameCacheHit = t.frameCacheHitRate;
    out.pipelineHit = t.pipelineHitRate;
    out.workerCount = t.workerCount;
    out.passesExecuted = t.frame.passesExecuted;
    out.passesCulled = t.frame.passesCulled;
    out.drawCalls = t.frame.drawCalls;
    out.triangles = t.frame.triangles;
    out.particles = t.frame.particles;
    out.shaderCount = t.shaderCount;
    out.pipelineCount = t.pipelineCount;
    out.shaderFailures = t.shaderFailures;
    out.activeEffects = t.effectsInPreviewMode;
    out.activeLayers = t.frame.layersRendered;
    out.adaptiveChanges = t.adaptiveScaleChanges;
    out.physicalResources = t.physicalResources;
    out.logicalResources = t.logicalResources;
    out.thermal = static_cast<f32>(t.thermal);
    out.throttling = t.throttling ? 1u : 0u;
    out.gpuMemoryBytes = t.frame.gpuMemoryBytes;
    out.cpuMemoryBytes = t.frame.cpuMemoryBytes;
    out.undoBlobBytes = t.undoBlobBytes;
    out.commandsDropped = t.commandsDropped;
    out.framesInFlight = gpu_ ? gpu_->frames_in_flight() : 0;
}

void Engine::fill_export_progress(bridge::ExportProgressPOD& out) const noexcept {
    out = bridge::ExportProgressPOD{};
    if (!exportCtx_) {
        out.result = static_cast<i32>(Errc::InvalidState);
        return;
    }
    const ExportProgress p = export_progress();
    out.running = p.running ? 1u : 0u;
    out.finished = p.finished ? 1u : 0u;
    out.result = static_cast<i32>(p.result);
    out.framesTotal = p.framesTotal;
    out.framesDone = p.framesDone;
    out.fps = p.fps;
    out.etaSeconds = p.etaSeconds;
    out.flags = p.flags;
    std::snprintf(out.message, sizeof(out.message), "%s", p.message);
}

} // namespace aurea
