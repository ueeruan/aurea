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
// ExportContext — o export entra na próxima fase, reusando `render_offscreen`.
// -----------------------------------------------------------------------------
struct Engine::ExportContext {
    ExportProgress progress{};
    ExportSettings settings{};
    std::string    outputPath;
    std::atomic<bool> cancelRequested{false};
};

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

    stop_render_thread();
    thumbs_.stop();
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
    request_render();
    return OkStatus;
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
    if (s.ok()) request_render();
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
        history_.clear();
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
        selection_.clear();
        if (Composition* c = current_composition()) {
            adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
            playback_ = PlaybackController{};
            playback_.configure(c->fps(), c->duration());
        }
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
    }
    const AssetId assetId = project_->add_asset(std::move(asset));

    // Primeiro clipe: a composição adota o vídeo (tamanho par, dentro do que
    // o aparelho exporta).
    const bool first = comp->layers().count() == 0;
    if (first) {
        u32 w = std::min(dispW, caps_.max_export_width()) & ~1u;
        u32 h = std::min(dispH, caps_.max_export_height()) & ~1u;
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

Result<u64> Engine::import_image(const u8* rgba, u32 width, u32 height, const char* name) noexcept {
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
    if (const Status s = renderer_.initialize(*gpu_, effectRegistry_); !s.ok()) return s;
    if (surface_.nativeWindow) {
        const Status s = gpu_->attach_surface(surface_);
        surfaceAttached_ = s.ok();
        if (!s.ok()) return s;
    }
    return OkStatus;
}

Status Engine::render_frame(bool onlyIfChanged) noexcept {
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
        t = playback_.update(frameStart);
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
        const bool force = forceRender_.exchange(false, std::memory_order_acq_rel);
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

    if (!surfaceAttached_) return OkStatus;

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
        row.reserved = 0;
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
            const i64 local = l->local_time(FrameIndex{timelineFrame}).value;
            const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
            const i64 us = static_cast<i64>(std::llround(static_cast<f64>(std::max<i64>(0, local)) * 1e6 / fps));
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
// Export — a próxima fase. A superfície de API existe; o codificador não.
// =============================================================================
Status Engine::start_export(const ExportSettings& settings, const char* outputPath) noexcept {
    if (!project_) return Errc::InvalidState;
    if (!outputPath) return Errc::InvalidArgument;
    if (!exportCtx_) exportCtx_ = std::make_unique<ExportContext>();
    exportCtx_->settings = settings;
    exportCtx_->outputPath = outputPath;
    exportCtx_->cancelRequested.store(false, std::memory_order_release);
    exportCtx_->progress = ExportProgress{};
    exportCtx_->progress.result = Errc::NotImplemented;
    std::snprintf(exportCtx_->progress.message, sizeof(exportCtx_->progress.message),
                  "%s", "exportacao ainda nao implementada");
    return Status{Errc::NotImplemented, "exportacao de video ainda nao implementada nesta fase"};
}

Status Engine::cancel_export() noexcept {
    if (!exportCtx_) return Errc::InvalidState;
    exportCtx_->cancelRequested.store(true, std::memory_order_release);
    return OkStatus;
}

Engine::ExportProgress Engine::export_progress() const noexcept {
    if (!exportCtx_) return ExportProgress{};
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
        || type == CommandType::CompositionSetDuration || type == CommandType::CompositionSetBackground;
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
            if (recordUndo) AUREA_LOG_WARN("undo de remocao de camada ainda sem payload");
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
                s->offset = FrameIndex{originalOffset.value + (at.value - l->start.value)};
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
        case CommandType::AudioSetMuted: {
            Layer* l = need_layer(cmd.audio_flag.layer);
            if (!l) return Errc::NotFound;
            l->muted = cmd.audio_flag.flag;
            return OkStatus;
        }
        case CommandType::AudioSetSolo: {
            Layer* l = need_layer(cmd.audio_flag.layer);
            if (!l) return Errc::NotFound;
            l->solo = cmd.audio_flag.flag;
            if (comp) {
                bool anySolo = false;
                comp->layers().for_each([&anySolo](LayerId, const Layer& other) { if (other.solo) anySolo = true; });
                comp->layers().for_each([anySolo](LayerId, Layer& other) {
                    other.muted = anySolo ? !other.solo : other.muted;
                });
            }
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
            if (cmd.comp_size.width > caps_.max_export_width() || cmd.comp_size.height > caps_.max_export_height()) {
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
            c->set_fps(cmd.comp_fps.fps);
            if (cmd.comp_fps.comp == timeline.current()) timeline.clock().set_fps(cmd.comp_fps.fps);
            return OkStatus;
        }
        case CommandType::CompositionSetDuration: {
            Composition* c = timeline.composition(cmd.comp_duration.comp);
            if (!c) return Errc::NotFound;
            c->set_duration(cmd.comp_duration.duration);
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
            return Status{Errc::NotImplemented, "exportacao de video ainda nao implementada nesta fase"};
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
    out.thumbnailGeneration = thumbs_.generation();
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
    const ExportProgress& p = exportCtx_->progress;
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
