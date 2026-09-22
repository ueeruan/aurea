#include "aurea/Engine.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/project/Serialization.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aurea {
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
// ExportContext — uma sessão de export (thread, sink, alvos de GPU, progresso).
// -----------------------------------------------------------------------------
struct Engine::ExportContext {
    mutable std::mutex mutex;          ///< protege `progress`
    ExportProgress progress{};
    ExportSettings settings{};
    std::string    outputPath;
    std::atomic<bool> cancelRequested{false};
    std::thread    thread;
    std::unique_ptr<ExportSink> sink;

    // Plano da sessão, fixado no start (a timeline não muda durante o export:
    // o preview e os comandos da UI ficam congelados).
    u32 width = 0, height = 0;
    f64 fps = 30.0;
    f64 compFps = 30.0;
    u32 frames = 0;
    bool dither = true;
    TextureHandle comp{}, y{}, uv{};
    std::vector<u8> yBytes, uvBytes;
    // Áudio: o MESMO mixer do preview, com um cache próprio que decodifica
    // na hora (o export não disputa decoder com o preview).
    std::unique_ptr<audio::AudioBlockCache> audioCache;
    std::shared_ptr<audio::AudioMixSnapshot> audioSnap;
    i64 audioWritten = 0;
    std::vector<f32> audioMix;
    std::vector<i16> audioPcm;
    u64 waitNs = 0, renderNs = 0, readNs = 0, writeNs = 0;
    u32 attempts = 0;

    void set_message(const char* m) {
        std::snprintf(progress.message, sizeof(progress.message), "%s", m);
    }
};

/// Maior lado de textura de modelo 3D no celular: 2048 (uma 4K com mips são
/// ~90 MB de GPU; um personagem com cinco delas estoura a memória de um
/// aparelho médio). O MESMO teto no import e ao reabrir — o quadro não muda.
constexpr u32 kModelTextureCap = 2048;

Engine::Engine() {
    commandQueue_ = std::make_unique<CommandQueue>();
    adaptive_ = new AdaptiveResolutionController(caps_);
}

Engine::~Engine() {
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
    caps_.detect();

    const u32 workers = config.workerCount ? config.workerCount : caps_.recommended_worker_count();
    if (const Status s = jobs_.start(workers); !s.ok()) {
        lastError_ = s.code();
        state_ = EngineState::Failed;
        return s;
    }

    const u64 budget = config.memoryBudgetBytes ? config.memoryBudgetBytes : caps_.memory_budget_bytes();
    memory_.set_budget(MemoryClass::Thumbnails,     budget * 6 / 100);
    memory_.set_budget(MemoryClass::Proxies,        budget * 10 / 100);
    memory_.set_budget(MemoryClass::DecodedFrames,  budget * 24 / 100);
    memory_.set_budget(MemoryClass::RenderedFrames, budget * 16 / 100);
    memory_.set_budget(MemoryClass::GpuTextures,    budget * 20 / 100);
    memory_.set_budget(MemoryClass::GpuGeometry,    budget * 10 / 100);
    memory_.set_budget(MemoryClass::Audio,          budget * 4 / 100);
    memory_.set_budget(MemoryClass::Assets,         budget * 8 / 100);
    memory_.set_budget(MemoryClass::Persistent,     budget * 2 / 100);

    if (effectRegistry_.count() == 0) register_builtin_effects(effectRegistry_);

    gpu_.reset(config.backend);
    renderer_.set_model_lookup(&Engine::model_lookup, this);
    if (gpu_) {
        // O backend guarda o caminho do cache de pipeline: por padrão, o mesmo
        // diretório de cache do motor (que vive em config_, não no chamador).
        BackendConfig bc = config_.backendConfig;
        if (!bc.cacheDirectory || !*bc.cacheDirectory) bc.cacheDirectory = config_.cacheDirectory.c_str();
        if (const Status s = gpu_->initialize(bc); !s.ok()) {
            AUREA_LOG_ERROR("backend grafico nao inicializou: %s", s.message().data());
            gpu_.reset();
        } else if (const Status r = renderer_.initialize(*gpu_, effectRegistry_); !r.ok()) {
            AUREA_LOG_ERROR("renderer nao inicializou: %s", r.message().data());
            gpu_->shutdown();
            gpu_.reset();
        } else {
            AUREA_LOG_INFO("GPU: %s", gpu_->capabilities().summary().c_str());
        }
    }

    media_.set_factory(config.mediaFactory);
    media_.set_ready_callback(&Engine::on_frame_ready, this);
    // Som: cache de blocos na verba de áudio; com saída, o áudio passa a ser o
    // relógio mestre do playback.
    audio_.initialize(config.mediaFactory, config.audioOutput, memory_.budget(MemoryClass::Audio));
    playback_.clock().set_master(&audio_);
    waveforms_ = std::make_unique<audio::WaveformCache>(config.mediaFactory);
    thumbs_.set_factory(config.mediaFactory);
    if (config.mediaFactory) thumbs_.start();

    adapt().configure(1920, 1080, config.displayRefreshRate);
    adapt().set_user_scale(config.initialPreviewScale);

    state_ = EngineState::Ready;
    AUREA_LOG_INFO("Aurea Engine pronta: %s", caps_.summary().c_str());
    return OkStatus;
}

void Engine::shutdown() noexcept {
    if (state_ == EngineState::Uninitialized || state_ == EngineState::ShuttingDown) return;
    state_ = EngineState::ShuttingDown;

    if (exportCtx_ && exportCtx_->thread.joinable()) {
        exportCtx_->cancelRequested.store(true, std::memory_order_release);
        exportCtx_->thread.join();
    }
    stop_render_thread();
    thumbs_.stop();
    playback_.clock().set_master(nullptr);
    audio_.shutdown();
    waveforms_.reset();
    media_.close_all();
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

    commandQueue_->reset();
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        project_.reset();
        images_.clear();
        models_.clear();
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

Status Engine::resume() noexcept {
    if (state_ != EngineState::Suspended) return Status{Errc::InvalidState, "motor nao esta suspenso"};
    media_.resume_all();
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
    if (gpu_) gpu_->detach_surface();
    surface_.nativeWindow = nullptr;
}

Status Engine::resize_surface(u32 width, u32 height) noexcept {
    std::lock_guard<std::mutex> rl(renderMutex_);
    surface_.width = width;
    surface_.height = height;
    if (!gpu_ || !surfaceAttached_) return OkStatus;
    const Status s = gpu_->resize_surface(width, height);
    forceRender_.store(true, std::memory_order_release);
    request_render();
    return s;
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
            } else {
                // Parado: dorme até ter o que mostrar.
                wakeCv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
                    return !renderRunning_ || wakeFlag_ || playingHint_.load();
                });
            }
            wakeFlag_ = false;
        }
        if (!renderRunning_) break;
        if (!surfaceAttached_ || state_ == EngineState::Suspended) continue;
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

    media_.close_all();
    thumbs_.clear();
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        renderer_.release_project_resources();
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    project_ = std::make_unique<Project>(std::move(*result));
    images_.clear();
    models_.clear();
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

Status Engine::load_project(const char* path) noexcept {
    if (!path) return Errc::InvalidArgument;

    Project loaded;
    LoadReport report;
    std::string error;
    LoadOptions options;
    options.lazyAssets = true;
    options.tolerateCorruptSections = true;
    const Status s = ProjectSerializer::load(loaded, path, options, &report, &error);
    if (!s.ok() && report.sectionsRead.empty()) return s;

    media_.close_all();
    thumbs_.clear();
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        renderer_.release_project_resources();
    }
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        project_ = std::make_unique<Project>(std::move(loaded));
        images_.clear();
        models_.clear();
        history_.clear();
        modelRevision_.fetch_add(1, std::memory_order_acq_rel);
        selection_.clear();
        if (Composition* c = current_composition()) {
            adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
            playback_ = PlaybackController{};
            playback_.configure(c->fps(), c->duration());
        }
    }
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
            if (!config_.imageLoader(src.c_str(), px, config_.imageLoaderContext) || px.width == 0 || px.height == 0
                || px.rgba.size() != static_cast<usize>(px.width) * px.height * 4) {
                ++missing;
                continue;
            }
            std::lock_guard<std::mutex> lock(modelMutex_);
            images_[key] = std::move(px);
        }
        if (missing) AUREA_LOG_WARN("%u imagem(ns) do projeto nao puderam ser abertas", missing);
    }
    // Modelos 3D: reabertos do caminho guardado (relativo ao sandbox). Ausente
    // = o projeto abre mesmo assim; a layer fica sem desenhar e a UI mostra
    // "modelo 3D ausente" para religar.
    {
        std::vector<std::pair<u64, std::string>> pending;
        {
            std::lock_guard<std::mutex> lock(modelMutex_);
            project_->for_each_asset([&](AssetId id, const Asset& a) {
                if (a.kind == AssetKind::Model3D && !a.sourcePath.empty()) pending.emplace_back(id.pack(), a.sourcePath);
            });
        }
        u32 missing = 0;
        for (const auto& [key, src] : pending) {
            scene3d::ImportOptions o;
            o.maxTextureSize = kModelTextureCap;
            scene3d::ImportResult r = scene3d::import_scene_file(resolve_asset_path(src), o);
            if (!r.ok()) {
                ++missing;
                AUREA_LOG_WARN("modelo 3D do projeto nao abriu: %s (%s)", src.c_str(), r.detail.c_str());
                continue;
            }
            std::lock_guard<std::mutex> lock(modelMutex_);
            models_[key] = std::shared_ptr<const scene3d::SceneAsset>(std::move(r.asset));
        }
        if (missing) AUREA_LOG_WARN("%u modelo(s) 3D ausente(s) no projeto", missing);
    }
    request_render();
    if (!report.clean()) return Status{Errc::CorruptData, "projeto aberto parcialmente"};
    return OkStatus;
}

Status Engine::save_project(const char* path) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Errc::InvalidState;
    if (!path) return Errc::InvalidArgument;
    if (const Status s = project_->save(path); !s.ok()) return s;
    project_->discard_recovery();
    return OkStatus;
}

Status Engine::save_project() noexcept {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Errc::InvalidState;
        if (!project_->has_path()) return Status{Errc::InvalidState, "projeto nunca foi salvo"};
        path = project_->path();
    }
    return save_project(path.c_str());
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
    if (dispW == 0 || dispH == 0) return Status{Errc::CorruptData, "video sem dimensoes"};

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
    }
    const AssetId assetId = project_->add_asset(std::move(asset));

    // Primeiro clipe: a composição adota o vídeo (tamanho par, dentro do que
    // o aparelho exporta).
    const bool first = comp->layers().count() == 0;
    if (first) {
        // O teto do aparelho é "lado maior × lado menor" (um 1920×1080 também
        // exporta 1080×1920). Aplicado por eixo, um vídeo em pé viraria uma
        // composição quadrada; aqui a escala é UMA só, e a proporção fica.
        const u32 capLong = std::max(caps_.max_export_width(), caps_.max_export_height());
        const u32 capShort = std::min(caps_.max_export_width(), caps_.max_export_height());
        const u32 vLong = std::max(dispW, dispH), vShort = std::min(dispW, dispH);
        const f64 k = std::min({1.0, static_cast<f64>(capLong) / vLong, static_cast<f64>(capShort) / vShort});
        u32 w = static_cast<u32>(std::lround(dispW * k)) & ~1u;
        u32 h = static_cast<u32>(std::lround(dispH * k)) & ~1u;
        if (w == 0) w = 2;
        if (h == 0) h = 2;
        comp->set_size(w, h);
        comp->set_fps(v.fps > 0.0 ? v.fps : 30.0);
    }
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
    if (probe.audioDurationUs <= 0) return Status{Errc::CorruptData, "audio sem duracao"};

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
std::string Engine::store_asset_path(const std::string& absolute) const {
    const std::string& docs = config_.documentsDirectory;
    if (!docs.empty() && absolute.size() > docs.size() && absolute.compare(0, docs.size(), docs) == 0) {
        usize start = docs.size();
        while (start < absolute.size() && (absolute[start] == '/' || absolute[start] == '\\')) ++start;
        return "docs:" + absolute.substr(start);
    }
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
        audio_.set_snapshot(audio::build_snapshot(comp, *project_, audio_.cache(), &Engine::audio_path_resolver, this));
    }
    // Velocidade ≠ 1 ainda sem time stretch (fase 6): o som sai de cena e o
    // relógio do sistema conduz — melhor mudo que tocando na velocidade errada.
    const bool want = playback_.playing() && std::fabs(playback_.speed() - 1.0f) < 1e-3f
                   && !exportActive_.load(std::memory_order_acquire);
    if (want) {
        if (!audio_.playing() || playback_.generation() != audioGeneration_) {
            audio_.play(playback_.current_ns());
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
        std::string base = config_.documentsDirectory;
        if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
        return base + stored.substr(5);
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
    options.maxTextureSize = kModelTextureCap;
    options.maxTextureSize = std::min<u32>(4096, caps_.max_export_width() > 0 ? 4096u : 2048u);
    scene3d::ImportResult r = scene3d::import_scene_file(request.path, options, progress);
    if (!r.ok()) {
        if (detail) *detail = r.detail;
        const Errc code = r.error == scene3d::ImportError::Cancelled ? Errc::Cancelled
                        : r.error == scene3d::ImportError::FileNotFound ? Errc::NotFound
                        : r.error == scene3d::ImportError::OutOfMemory ? Errc::OutOfMemory
                        : r.error == scene3d::ImportError::UnsupportedCompression
                          || r.error == scene3d::ImportError::UnsupportedFeature ? Errc::UnsupportedFeature
                        : Errc::CorruptData;
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
    asset.model.lodCount = 1;
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

std::shared_ptr<const scene3d::SceneAsset> Engine::model_asset(u64 assetId) const noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const auto it = models_.find(assetId);
    return it == models_.end() ? nullptr : it->second;
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
    const EngineState s = state_;
    if (s != EngineState::Ready && s != EngineState::Rendering && s != EngineState::Suspended) return 0;

    u32 accepted = 0;
    for (u32 i = 0; i < count; ++i) {
        if (commandQueue_->push(commands[i]) == kInvalidIndex) break;
        ++accepted;
    }
    if (stringBlob && stringBlobSize) {
        if (char* dst = commandQueue_->alloc_string(stringBlobSize)) {
            std::memcpy(dst, stringBlob, stringBlobSize);
        }
    }
    commandQueue_->commit();
    request_render();
    return accepted;
}

void Engine::drain_commands_locked() noexcept {
    if (!project_) return;
    const u32 n = commandQueue_->drain([this](const Command& cmd) {
        const char* str = nullptr;
        if (cmd.stringLength > 0) str = commandQueue_->string_at(cmd.stringOffset, cmd.stringLength);
        const Status s = apply_command_internal(cmd, str, true);
        if (!s.ok()) {
            AUREA_LOG_WARN("comando %u recusado: %s", static_cast<unsigned>(cmd.type), s.message().data());
        }
    });
    if (n > 0) project_->mark_dirty();
}

RenderSettings Engine::current_render_settings() noexcept {
    RenderSettings rs;
    rs.previewNumerator = adapt().current_numerator();
    rs.previewDenominator = adapt().current_denominator();
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

    RenderSettings rs;
    FrameIndex t{0};
    bool playing = false;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Errc::InvalidState;
        drain_commands_locked();
        Composition* comp = current_composition();
        if (!comp) return Errc::NotFound;

        playback_.configure(comp->fps(), comp->duration());
        // Antes do update: um seek/play desta leva recomeça o som no ponto
        // novo (senão o relógio do áudio ainda diria o instante antigo).
        sync_audio_locked(*comp);
        t = playback_.update(frameStart);
        // Depois: o loop acontece dentro do update.
        sync_audio_locked(*comp);
        project_->timeline().set_playhead(t);
        playing = playback_.playing();
        playingHint_ = playing;

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
        if (onlyIfChanged && !force && t.value == lastRenderedFrame_
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
        renderer_.prepare(*comp, *project_, t, &media_, &Engine::image_lookup, this, rs, ++frameCounter_,
                          playback_.direction(), mode, playback_.speed(), snapshot_);
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
    const Status s = renderer_.render(snapshot_, rs, nullptr, stats, timings);
    if (!s.ok() && s.code() != Errc::SurfaceLost && s.code() != Errc::Timeout) {
        AUREA_LOG_WARN("frame nao renderizado: %s", s.message().data());
    }
    if (s.code() == Errc::SurfaceLost || s.code() == Errc::Timeout) request_render();

    lastRenderedFrame_ = t.value;
    lastIncomplete_ = snapshot_.missingVideoFrames > 0 || snapshot_.staleVideoFrames > 0;
    if (!s.ok()) forceRender_.store(true, std::memory_order_release);
    frameScheduler_.presented(t, playing);
    stats.frameIndex = static_cast<u32>(frameCounter_);
    stats.cpuMs = timings.cpuPrepareMs + timings.cpuRecordMs;
    stats.decodeMs = media_.stats().decodeMsAvg;
    stats.droppedFrames = frameScheduler_.dropped_total();
    stats.cpuMemoryBytes = memory_.total_used();
    (void)adapt().update(stats, caps_.thermal());
    media_.collect(frameCounter_);
    update_perf(stats, timings, snapshot_, frameStart);
    return s;
}

Status Engine::render_offscreen(TextureHandle target, u32 width, u32 height) noexcept {
    std::lock_guard<std::mutex> rl(renderMutex_);
    if (!gpu_ || !renderer_.ready()) return Status{Errc::InvalidState, "sem GPU"};

    RenderSettings rs;
    rs.dither = false;
    rs.gpuTimers = false;
    // Espera os frames EXATOS de vídeo (export e teste não aceitam o frame
    // aproximado que o scrub mostra). Limite de 4 s para arquivo quebrado não
    // travar para sempre.
    for (int attempt = 0; attempt < 800; ++attempt) {
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
        if (snapshot_.missingVideoFrames == 0 && snapshot_.staleVideoFrames == 0) break;
        for (RenderLayer& l : snapshot_.layers) l.source.frame.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    OffscreenTarget off{target, width, height};
    FrameStats stats;
    RenderTimings timings;
    const Status s = renderer_.render(snapshot_, rs, &off, stats, timings);
    gpu_->wait_idle();
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
    const u64 elapsed = frameStartNs - fpsWindowStartNs_;
    if (elapsed >= 1'000'000'000ull) {
        measuredFps_ = static_cast<f32>(static_cast<f64>(fpsWindowFrames_) * 1e9 / static_cast<f64>(elapsed));
        fpsWindowFrames_ = 0;
        fpsWindowStartNs_ = frameStartNs;
        frameScheduler_.roll_window();
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

    // A partir do primeiro frame mostrado, pipeline novo é "durante o
    // playback" — o número certo no painel é zero.
    if (renderer_.frames_rendered() == 1) renderer_.shaders().mark_steady_state();

    std::lock_guard<std::mutex> lock(perfMutex_);
    perf_ = p;
    lastFrame_ = stats;
    lastFrame_.gpuMs = p.gpuFrameMs;
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
    t.undoBlobBytes = 0;   // snapshots de composição: KB por ação, contados por profundidade
    t.commandsDropped = commandQueue_->dropped_count();
    t.thermal = caps_.thermal().level;
    t.throttling = caps_.thermal().throttling;
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
        row.opacity = l->transform.opacity;
        row.effectCount = static_cast<u32>(l->effects.size());
        row.maskCount = static_cast<u32>(l->masks.size());
        u32 keyCount = 0;
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
        row.flags = flags;
        row.parentIndex = kInvalidIndex;
        if (outNameBlob && nameCursor + l->name.size() < nameBlobCapacity) {
            std::memcpy(outNameBlob + nameCursor, l->name.data(), l->name.size());
            row.nameOffset = nameCursor;
            row.nameLength = static_cast<u32>(l->name.size());
            nameCursor += static_cast<u32>(l->name.size());
        }
        out[written++] = row;
    }
    return written;
}

u32 Engine::query_keyframes(u64 layerId, bridge::KeyframeRow* out, u32 capacity) noexcept {
    if (!out) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = current_composition();
    if (!comp) return 0;
    Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return 0;
    u32 written = 0;
    for (u32 t = 0; t < l->tracks.size() && written < capacity; ++t) {
        const Track& track = l->tracks.at(t);
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
    }
    return written;
}

u32 Engine::query_waveform(u64 layerId, f64 startFrame, f64 framesPerBucket, u32 count, u8* out) noexcept {
    if (!out || count == 0 || !waveforms_ || !(framesPerBucket > 0.0)) return 0;
    u64 key = 0;
    f64 srcStart = 0.0, perBucket = 0.0;
    bool reversedWave = false;
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
        if (l->speed <= 0.0f) return 0;   // quadro congelado: sem som
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

bool Engine::query_layer_detail(u64 layerId, bridge::LayerDetailPOD& out) noexcept {
    out = bridge::LayerDetailPOD{};
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = current_composition();
    if (!comp || !project_) return false;
    const Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return false;
    const FrameIndex local = l->local_time(playback_.current());
    auto value = [&](TrackProperty p, f32 fallback, u32 bit) noexcept {
        const Track* tr = l->tracks.find(p);
        if (!tr || tr->keys.empty()) return fallback;
        out.animatedMask |= 1u << bit;
        if (tr->find_exact(local) != kInvalidIndex) out.keyAtPlayheadMask |= 1u << bit;
        return tr->sample(local);
    };
    using TP = TrackProperty;
    const Transform& tf = l->transform;
    out.id = layerId;
    out.kind = static_cast<u32>(l->kind);
    bool selected = false;
    for (u64 s : selection_) if (s == layerId) selected = true;
    out.flags = (l->visible ? bridge::kLayerRowFlagVisible : 0u) | (l->locked ? bridge::kLayerRowFlagLocked : 0u)
              | (selected ? bridge::kLayerRowFlagSelected : 0u);
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
    out.speed = l->speed;
    out.timeFlags = l->reversed ? 1u : 0u;
    {
        const Asset* aa = project_->asset(l->source);
        const Track* vt = l->tracks.find(TrackProperty::AudioVolume);
        out.audioFlags = (l->muted ? bridge::kAudioFlagMuted : 0u) | (l->solo ? bridge::kAudioFlagSolo : 0u)
                       | ((aa && aa->has_audio() && (l->kind == LayerKind::Video || l->kind == LayerKind::Audio))
                              ? bridge::kAudioFlagHasAudio : 0u)
                       | ((vt && vt->animated()) ? bridge::kAudioFlagVolumeAnimated : 0u);
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

u32 Engine::query_curve(u64 layerId, u32 property, i32 startFrame, i32 endFrame,
                        f32* outValues, u32 sampleCount) noexcept {
    if (!outValues || sampleCount == 0 || endFrame <= startFrame) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = current_composition();
    if (!comp) return 0;
    Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return 0;
    const Track* track = l->tracks.find(static_cast<TrackProperty>(property));
    if (!track) return 0;
    const f64 step = static_cast<f64>(endFrame - startFrame) / static_cast<f64>(sampleCount > 1 ? sampleCount - 1 : 1);
    for (u32 i = 0; i < sampleCount; ++i) {
        outValues[i] = track->sample(FrameIndex{static_cast<i64>(static_cast<f64>(startFrame) + step * i)});
    }
    return sampleCount;
}

u32 Engine::query_effect_catalog(bridge::EffectCatalogRow* out, u32 capacity, char* blob,
                                 u32 blobCapacity) noexcept {
    if (!out) return 0;
    u32 cursor = 0, written = 0;
    for (u32 i = 0; i < effectRegistry_.count() && written < capacity; ++i) {
        const Effect& e = effectRegistry_.at(i);
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
    Composition* comp = current_composition();
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
    Composition* comp = current_composition();
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
        const ParamSpec& spec = params->at(i);
        bridge::EffectParamRow row;
        row.index = i;
        row.type = static_cast<u32>(spec.type);
        row.flags = spec.flags;
        row.enumCount = spec.enumCount;
        row.minValue = spec.minValue;
        row.maxValue = spec.maxValue;
        const ParamValue v = evaluate_param(l->tracks, *inst, i, spec, local);
        for (int c = 0; c < 4; ++c) {
            row.value[c] = v.v[c];
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
        const u32 comps = component_count(spec.type);
        for (u32 c = 0; c < comps; ++c) {
            const Track* t = l->tracks.find(TrackProperty::EffectParam, inst->id, param_track_key(i, c));
            if (t && !t->keys.empty()) row.animated = 1;
        }
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
Status Engine::start_export(const ExportSettings& settings, const char* outputPath) noexcept {
    if (!project_) return Errc::InvalidState;
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
        const f64 seconds = comp->duration_seconds();
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

    ctx->sink = config_.exportSinkFactory(config_.exportSinkContext);
    if (!ctx->sink) return Status{Errc::NotSupported, "encoder indisponivel"};

    VideoStreamConfig vc;
    vc.width = ctx->width;
    vc.height = ctx->height;
    vc.fps = ctx->fps;
    vc.codec = settings.videoCodec;
    // Mbps do projeto, ou proporcional aos pixels (0,2 bit por pixel·s:
    // 1080p30 ≈ 12 Mbps, 4K30 ≈ 50 Mbps) — generoso, é arquivo de edição.
    const f64 pixelsPerSecond = static_cast<f64>(ctx->width) * ctx->height * ctx->fps;
    vc.bitrateBps = settings.videoBitrateMbps > 0
                  ? settings.videoBitrateMbps * 1000000u
                  : static_cast<u32>(std::clamp(pixelsPerSecond * 0.2, 2.0e6, 120.0e6));
    vc.keyframeIntervalFrames = settings.keyframeIntervalFrames;
    // Com som na timeline, o arquivo leva AAC; sem, só vídeo (uma trilha de
    // silêncio não serve para nada e alguns players a mostram como "com som").
    AudioStreamConfig ac;
    ac.sampleRate = audio::kMixRate;
    ac.channels = audio::kMixChannels;
    ac.bitrateBps = std::clamp<u32>(settings.audioBitrateKbps, 64, 320) * 1000u;
    const bool withAudio = ctx->audioSnap && ctx->audioSnap->audible();
    if (!withAudio) ctx->audioSnap.reset();
    if (const Status s = ctx->sink->open(outputPath, vc, withAudio ? &ac : nullptr); !s.ok()) return s;

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
    }
    ctx->yBytes.resize(static_cast<usize>(ctx->width) * ctx->height);
    ctx->uvBytes.resize(static_cast<usize>(ctx->width) * (ctx->height / 2));

    ctx->progress.running = true;
    ctx->progress.framesTotal = ctx->frames;
    ctx->set_message("exportando");
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        playback_.pause(monotonic_ns());
        playingHint_ = false;
        audio_.stop();
    }
    exportCtx_ = std::move(ctx);
    exportActive_.store(true, std::memory_order_release);
    exportCtx_->thread = std::thread([this] { export_thread_main(); });
    return OkStatus;
}

Status Engine::render_export_frame(FrameIndex t, const OffscreenTarget& target) noexcept {
    std::lock_guard<std::mutex> rl(renderMutex_);
    if (!gpu_ || !renderer_.ready()) return Status{Errc::InvalidState, "sem GPU"};
    RenderSettings rs;
    rs.dither = false;
    rs.gpuTimers = false;
    // Quadros EXATOS de vídeo, em sequência (o modo Playback decodifica
    // adiante). 4 s de tolerância por quadro: arquivo quebrado não trava.
    const u64 t0 = monotonic_ns();
    int attempts = 0;
    for (int attempt = 0; attempt < 800; ++attempt) {
        ++attempts;
        {
            std::lock_guard<std::mutex> lock(modelMutex_);
            if (!project_) return Errc::InvalidState;
            Composition* comp = current_composition();
            if (!comp) return Errc::NotFound;
            renderer_.prepare(*comp, *project_, t, &media_, &Engine::image_lookup, this, rs, ++frameCounter_,
                              1, DecodeMode::Playback, 1.0f, snapshot_);
        }
        if (snapshot_.missingVideoFrames == 0 && snapshot_.staleVideoFrames == 0) break;
        for (RenderLayer& l : snapshot_.layers) l.source.frame.reset();
        if (exportCtx_->cancelRequested.load(std::memory_order_acquire)) return Errc::Cancelled;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const u64 t1 = monotonic_ns();
    FrameStats stats;
    RenderTimings timings;
    if (const Status s = renderer_.render(snapshot_, rs, &target, stats, timings); !s.ok()) return s;
    const u64 t2 = monotonic_ns();
    if (const Status r = gpu_->read_texture(target.yPlane, exportCtx_->yBytes.data(), exportCtx_->width); !r.ok()) {
        return r;
    }
    const Status r = gpu_->read_texture(target.uvPlane, exportCtx_->uvBytes.data(), exportCtx_->width);
    const u64 t3 = monotonic_ns();
    ExportContext& c = *exportCtx_;
    c.waitNs += t1 - t0;
    c.renderNs += t2 - t1;
    c.readNs += t3 - t2;
    c.attempts += static_cast<u32>(attempts);
    return r;
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
        audio::mix(*ctx.audioSnap, ctx.audioWritten, n, blocks, ctx.audioMix.data());
        audio::to_pcm16(ctx.audioMix.data(), static_cast<usize>(n) * audio::kMixChannels, ctx.audioPcm.data());
        const i64 ptsUs = audio::sample_to_ns(ctx.audioWritten) / 1000;
        if (const Status s = ctx.sink->write_audio(ctx.audioPcm.data(), n, ptsUs); !s.ok()) return s;
        ctx.audioWritten += n;
    }
    return OkStatus;
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

    const u64 start = monotonic_ns();
    Status result = OkStatus;
    for (u32 i = 0; i < ctx.frames; ++i) {
        if (ctx.cancelRequested.load(std::memory_order_acquire)) { result = Errc::Cancelled; break; }
        // Tempo de SAÍDA → quadro da composição (fps diferentes se encontram
        // pelo instante, não pelo índice).
        const f64 seconds = static_cast<f64>(i) / ctx.fps;
        const FrameIndex t{static_cast<i64>(std::floor(seconds * ctx.compFps + 1e-6))};
        result = render_export_frame(t, target);
        if (!result.ok()) break;
        const i64 pts = static_cast<i64>(std::llround(seconds * 1e6));
        const u64 w0 = monotonic_ns();
        result = ctx.sink->write_video(ctx.yBytes.data(), ctx.width, ctx.uvBytes.data(), ctx.width, pts);
        ctx.writeNs += monotonic_ns() - w0;
        if (!result.ok()) break;
        // O som até o fim DESTE quadro (em amostras inteiras: nenhuma deriva
        // acumulada, nem em 29,97).
        if (ctx.audioSnap) {
            result = write_export_audio(audio::frame_to_sample(static_cast<i64>(i) + 1, ctx.fps));
            if (!result.ok()) break;
        }
        // Diagnóstico a cada 300 quadros: onde o tempo do export está indo.
        if ((i + 1) % 300 == 0 || i + 1 == ctx.frames) {
            const f64 n = static_cast<f64>((i % 300) + 1);
            AUREA_LOG_INFO("export %u/%u: espera %.1f ms (%.1f tentativas) render %.1f leitura %.1f encoder %.1f ms/quadro",
                           i + 1, ctx.frames, ctx.waitNs / 1e6 / n, ctx.attempts / n, ctx.renderNs / 1e6 / n,
                           ctx.readNs / 1e6 / n, ctx.writeNs / 1e6 / n);
            ctx.waitNs = ctx.renderNs = ctx.readNs = ctx.writeNs = 0;
            ctx.attempts = 0;
        }

        const f64 elapsed = static_cast<f64>(monotonic_ns() - start) / 1e9;
        std::lock_guard<std::mutex> pl(ctx.mutex);
        ctx.progress.framesDone = i + 1;
        ctx.progress.fps = elapsed > 0.0 ? static_cast<f32>((i + 1) / elapsed) : 0.0f;
        ctx.progress.etaSeconds = ctx.progress.fps > 0.0f
                                ? static_cast<u32>((ctx.frames - (i + 1)) / ctx.progress.fps) : 0;
    }

    if (result.ok()) {
        result = ctx.sink->finish();
    } else {
        ctx.sink->abort();
    }
    ctx.sink.reset();
    {
        std::lock_guard<std::mutex> rl(renderMutex_);
        if (gpu_) {
            gpu_->destroy_texture(ctx.comp);
            gpu_->destroy_texture(ctx.y);
            gpu_->destroy_texture(ctx.uv);
        }
    }
    {
        std::lock_guard<std::mutex> pl(ctx.mutex);
        ctx.progress.running = false;
        ctx.progress.finished = true;
        ctx.progress.result = result.code();
        if (result.ok()) ctx.set_message("concluido");
        else if (result.code() == Errc::Cancelled) ctx.set_message("cancelado");
        else ctx.set_message(result.message().data());
    }
    if (!result.ok() && result.code() != Errc::Cancelled) {
        AUREA_LOG_ERROR("export falhou: %s", result.message().data());
    }
    exportActive_.store(false, std::memory_order_release);
    forceRender_ = true;
    request_render();
}

Status Engine::cancel_export() noexcept {
    if (!exportCtx_) return Errc::InvalidState;
    exportCtx_->cancelRequested.store(true, std::memory_order_release);
    return OkStatus;
}

Engine::ExportProgress Engine::export_progress() const noexcept {
    if (!exportCtx_) return ExportProgress{};
    std::lock_guard<std::mutex> pl(exportCtx_->mutex);
    return exportCtx_->progress;
}

// =============================================================================
// Aplicação de comandos
// =============================================================================
Status Engine::apply_command(const Command& cmd, const char* stringData) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Status s = apply_command_internal(cmd, stringData, true);
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
        || type == CommandType::LayerSetSpeed || type == CommandType::LayerSetReversed;
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

Status Engine::apply_command_internal(const Command& cmd, const char* stringData,
                                      bool recordUndo) noexcept {
    if (!project_) return Errc::InvalidState;

    Timeline& timeline = project_->timeline();
    Composition* comp = current_composition();
    const u64 now = monotonic_ns();

    auto need_layer = [&](LayerId id) -> Layer* { return comp ? comp->layer(id) : nullptr; };
    // O PlaybackController é a fonte da verdade; a timeline espelha o estado
    // para quem a consulta (serialização, estado da UI).
    auto sync_timeline = [&] {
        playingHint_ = playback_.playing();
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
            l->start = cmd.layer_range.start;
            l->end = cmd.layer_range.end;
            if (cmd.layer_range.setOffset) l->offset = cmd.layer_range.offset;
            return OkStatus;
        }

        case CommandType::LayerSplit: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l || !comp) return Errc::NotFound;
            const FrameIndex at = cmd.layer_split.at;
            if (at.value <= l->start.value || at.value >= l->end.value) return Errc::OutOfRange;
            const FrameIndex originalEnd = l->end;
            const FrameIndex originalOffset = l->offset;
            const LayerId second = comp->duplicate_layer(cmd.layer_ref.layer, at);
            if (!second.valid()) return Errc::OutOfMemory;
            l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            l->end = at;
            if (Layer* s = comp->layer(second)) {
                s->start = at;
                s->end = originalEnd;
                // Ponto de entrada de cada metade pela velocidade; no reverso a
                // primeira metade é a que começa mais tarde na fonte.
                const f64 sp = l->speed;
                if (l->reversed) {
                    s->offset = originalOffset;
                    l->offset = FrameIndex{originalOffset.value
                                           + static_cast<i64>(std::llround(static_cast<f64>(originalEnd.value - at.value) * sp))};
                } else {
                    s->offset = FrameIndex{originalOffset.value
                                           + static_cast<i64>(std::llround(static_cast<f64>(at.value - l->start.value) * sp))};
                }
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

        case CommandType::LayerSetParent: {
            Layer* l = need_layer(cmd.layer_parent.layer);
            if (!l) return Errc::NotFound;
            const LayerId parent = cmd.layer_parent.parent;
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
            l->parent = parent;
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
            Track& track = l->tracks.get_or_create(cmd.keyframe.track.property, cmd.keyframe.track.effectIndex,
                                                   cmd.keyframe.track.effectParamIndex);
            (void)track.set(cmd.keyframe.time, cmd.keyframe.value, Interpolation::Linear);
            return OkStatus;
        }
        case CommandType::KeyframeDelete: {
            Layer* l = need_layer(cmd.keyframe.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = l->tracks.find(cmd.keyframe.track.property, cmd.keyframe.track.effectIndex,
                                          cmd.keyframe.track.effectParamIndex);
            if (!track || !track->remove(cmd.keyframe.time)) return Errc::NotFound;
            return OkStatus;
        }
        case CommandType::KeyframeMove: {
            Layer* l = need_layer(cmd.keyframe_move.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = l->tracks.find(cmd.keyframe_move.track.property, cmd.keyframe_move.track.effectIndex,
                                          cmd.keyframe_move.track.effectParamIndex);
            if (!track) return Errc::NotFound;
            if (track->move(cmd.keyframe_move.fromTime, cmd.keyframe_move.toTime) == kInvalidIndex) return Errc::NotFound;
            return OkStatus;
        }
        case CommandType::KeyframeSetValue: {
            Layer* l = need_layer(cmd.keyframe.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = l->tracks.find(cmd.keyframe.track.property, cmd.keyframe.track.effectIndex,
                                          cmd.keyframe.track.effectParamIndex);
            if (!track) return Errc::NotFound;
            const u32 idx = track->find_exact(cmd.keyframe.time);
            if (idx == kInvalidIndex) return Errc::NotFound;
            track->keys[idx].value = cmd.keyframe.value;
            return OkStatus;
        }
        case CommandType::KeyframeSetInterpolation:
        case CommandType::KeyframeSetBezier:
        case CommandType::KeyframeSetEasing: {
            Layer* l = need_layer(cmd.keyframe_interp.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = l->tracks.find(cmd.keyframe_interp.track.property, cmd.keyframe_interp.track.effectIndex,
                                          cmd.keyframe_interp.track.effectParamIndex);
            if (!track) return Errc::NotFound;
            track->set_interpolation(cmd.keyframe_interp.time, cmd.keyframe_interp.interp,
                                     cmd.keyframe_interp.bx1, cmd.keyframe_interp.by1,
                                     cmd.keyframe_interp.bx2, cmd.keyframe_interp.by2);
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Máscaras (dados do modelo; o rasterizador de máscara é fase futura)
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
                if (it->id == cmd.mask_scalar.mask.index) { l->masks.erase(it); return OkStatus; }
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
            switch (cmd.type) {
                case CommandType::MaskSetFeather:   m->feather   = cmd.mask_scalar.value; break;
                case CommandType::MaskSetExpansion: m->expansion = cmd.mask_scalar.value; break;
                default:                            m->opacity   = clampf(cmd.mask_scalar.value, 0.0f, 1.0f); break;
            }
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
            const ParameterRegistry* params = effectRegistry_.params(type);
            if (!params) return Errc::NotSupported;
            EffectInstance e;
            e.id = l->alloc_effect_id();
            e.type = type;
            initialize_instance(e, *params);
            if (cmd.effect_add.index < l->effects.size()) {
                l->effects.insert(l->effects.begin() + cmd.effect_add.index, std::move(e));
            } else {
                l->effects.push_back(std::move(e));
            }
            return OkStatus;
        }
        case CommandType::EffectRemove: {
            Layer* l = need_layer(cmd.effect_ref.layer);
            if (!l) return Errc::NotFound;
            const u32 idx = l->effect_index(cmd.effect_ref.effect);
            if (idx == kInvalidIndex) return Errc::NotFound;
            const u32 id = l->effects[idx].id;
            l->effects.erase(l->effects.begin() + idx);
            // Os keyframes do efeito saem junto: órfãos ficariam no arquivo e
            // voltariam a animar um efeito novo que reusasse o id.
            for (u32 t = 0; t < l->tracks.size(); ++t) {
                Track& tr = l->tracks.at(t);
                if (tr.property == TrackProperty::EffectParam && tr.effectIndex == id) tr.clear();
            }
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
            return OkStatus;
        }
        case CommandType::EffectSetParam: {
            Layer* l = need_layer(cmd.effect_param.layer);
            if (!l) return Errc::NotFound;
            EffectInstance* e = l->find_effect(cmd.effect_param.effect);
            if (!e) return Errc::NotFound;
            const u32 p = cmd.effect_param.paramIndex;
            if (p >= e->params.size()) return Errc::OutOfRange;
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
            if (l->end.value > comp->duration().value) comp->set_duration(l->end);
            playback_.configure(comp->fps(), comp->duration());
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
            return OkStatus;
        }
        case CommandType::TextSetFont:
            return Status{Errc::NotImplemented, "troca de fonte ainda nao implementada"};
        case CommandType::TextSetSize: {
            Layer* l = need_layer(cmd.text_size.layer);
            if (!l) return Errc::NotFound;
            l->text.size = cmd.text_size.size;
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
            l->text.alignment = cmd.text_align.alignment;
            return OkStatus;
        }
        case CommandType::TextSetStrokeWidth: {
            Layer* l = need_layer(cmd.text_stroke_width.layer);
            if (!l) return Errc::NotFound;
            l->text.strokeWidth = cmd.text_stroke_width.width;
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
            c->set_size(cmd.comp_size.width, cmd.comp_size.height);
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
    std::snprintf(out.message, sizeof(out.message), "%s", p.message);
}

} // namespace aurea
