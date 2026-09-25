// =============================================================================
//  Aurea / platform / ios / bridge / AureaBridge.mm
//
//  O dono do motor no iOS. Este arquivo é BURRO de propósito, como o
//  aurea_jni.cpp do Android: traduz ciclo de vida e chamadas, e nada mais.
//  Toda regra vive no C++ compartilhado — é o que faz o preview e o export do
//  iOS serem o MESMO código do Android.
//
//  Compilado com ARC (`-fobjc-arc`): as referências ObjC que a ponte guarda
//  (a view, o display link) são fracas por natureza, e o que a ponte possui de
//  verdade (o Engine, a saída de áudio, a fábrica de mídia) é C++.
// =============================================================================
#include "AureaBridge.h"

#include "aurea/core/Log.hpp"
#include "aurea/core/Version.hpp"

#include <os/log.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(AUREA_GPU_METAL)
#include "MetalBackend.hpp"
#endif

using namespace aurea;

namespace aurea::ios {

namespace {

os_log_t g_log = nullptr;

void ios_log_sink(LogLevel level, const char* message, void*) {
    if (!g_log) g_log = os_log_create("com.aurea.aurea", "engine");
    // A mensagem do motor já vem formatada (Log.cpp). Nunca com formato de
    // printf: passá-la como formato daria crash num nome de camada com "%".
    os_log_type_t type = OS_LOG_TYPE_DEFAULT;
    switch (level) {
        case LogLevel::Trace: type = OS_LOG_TYPE_DEBUG;   break;
        case LogLevel::Debug: type = OS_LOG_TYPE_DEBUG;   break;
        case LogLevel::Info:  type = OS_LOG_TYPE_DEFAULT; break;
        case LogLevel::Warn:  type = OS_LOG_TYPE_DEFAULT; break;
        case LogLevel::Error: type = OS_LOG_TYPE_ERROR;   break;
        case LogLevel::Fatal: type = OS_LOG_TYPE_FAULT;   break;
    }
    os_log_with_type(g_log, type, "%{public}s", message ? message : "");
#if DEBUG
    // simctl launch --console captures process streams, not unified logging.
    // Keep CI's real renderer errors beside each parity scene without changing
    // normal device logging or including this diagnostics path in Release.
    static const bool parityConsole = std::getenv("AUREA_PARITY_SCENE") != nullptr;
    if (parityConsole) {
        std::fprintf(stderr, "%s\n", message ? message : "");
        std::fflush(stderr);
    }
#endif
}

/// Instala o sink UMA vez. O motor fala por aqui em qualquer aparelho; sem
/// isto o log do iOS some e um erro em produção não deixa rastro.
void install_log_sink_once() noexcept {
    static bool installed = false;
    if (installed) return;
    installed = true;
    set_log_sink(&ios_log_sink, nullptr);
    AUREA_LOG_INFO("Aurea iOS, nucleo v%s", AUREA_VERSION_LABEL);
}

} // namespace

// =============================================================================
// Batch
// =============================================================================
Command* Batch::add(CommandType type) noexcept {
    if (commands_.size() >= kMaxCommands) return nullptr;
    commands_.emplace_back();
    commands_.back().type = type;
    return &commands_.back();
}

StringRef Batch::add_string(const char* text) noexcept {
    if (!text || !*text) return {};
    const usize len = std::strlen(text);
    if (blob_.size() + len > kMaxBlobBytes) return {};
    const u32 offset = static_cast<u32>(blob_.size());
    blob_.append(text, len);
    // SEM terminador: o motor copia `stringLength` bytes e fecha a string do
    // lado dele (Engine::drain_commands_locked). Guardar o NUL aqui mudaria o
    // comprimento e o texto chegaria com um byte a mais.
    return StringRef{offset, static_cast<u32>(len)};
}

u32 Batch::submit(Engine& engine) noexcept {
    if (commands_.empty()) return 0;
    const u32 accepted = engine.submit_commands(commands_.data(), static_cast<u32>(commands_.size()),
                                                blob_.empty() ? nullptr : blob_.data(),
                                                static_cast<u32>(blob_.size()));
    // `submit_commands` é assíncrono por desenho (a thread de render drena), mas
    // ACORDAR a thread é trabalho de quem manda: o mesmo `request_render` do
    // nativeSubmitCommands do Android.
    engine.request_render();
    clear();
    return accepted;
}

// =============================================================================
// Host
// =============================================================================
Host::~Host() {
    // A ordem importa e é a mesma do Android: motor primeiro (ele para a thread
    // de render e devolve os decoders), superfície depois.
    shutdown();
}

Status Host::initialize(const std::string& cacheDirectory, const std::string& documentsDirectory,
                        void* nativeDevice, f32 displayRefreshRate, bool debug) noexcept {
    if (initialized_) return OkStatus;
    install_log_sink_once();

    cache_ = cacheDirectory;
    documents_ = documentsDirectory;

    media_ = make_video_factory(&mediaControl_);
    if (!media_) {
        lastError_ = "fabrica de midia do VideoToolbox indisponivel";
        AUREA_LOG_ERROR("%s", lastError_.c_str());
        return Status{Errc::NotSupported, "fabrica de midia (VideoToolbox) indisponivel"};
    }
    audioOut_ = make_audio_output();

    engine_ = std::make_unique<Engine>();
    if (!engine_) {
        lastError_ = "sem memoria para o motor";
        return Status{Errc::OutOfMemory, "sem memoria para o motor"};
    }

    EngineConfig config;
    // Backend Metal. Nulo = o app abre sem preview (a timeline, os comandos, a
    // serialização e o export continuam) — melhor que recusar a abrir.
    GPUBackend* backend = nullptr;
#if defined(AUREA_GPU_METAL)
    backend = mtl::create_backend(nativeDevice);
#else
    (void)nativeDevice;
#endif
    if (!backend) {
        AUREA_LOG_WARN("sem backend Metal: o preview nao tem como desenhar");
    }
    // A posse é do MOTOR (EngineConfig::backend); o Host nunca apaga este
    // ponteiro — só o consulta para calibrar o zero-copy.
    config.backend = backend;
    config.backendConfig.enableValidation = debug;
    // Opcional para o editor. Contadores de blit podem abortar no driver de
    // alguns iPhones ao abrir o primeiro frame; não arriscar a sessão por FPS.
    config.backendConfig.enableGpuTimers = false;
    config.cacheDirectory = cache_;
    config.documentsDirectory = documents_;
    config.displayRefreshRate = displayRefreshRate > 0.0f ? displayRefreshRate : 60.0f;

    PlatformInfo info;
    fill_platform_info(info);
    config.platformInfo = info;
    config.hasPlatformInfo = true;

    config.mediaFactory = media_.get();
    config.exportSinkFactory = &make_export_sink;
    config.audioOutput = audioOut_.get();
    config.defaultFontPath = ios_default_font_path();
    config.imageLoader = &ios_load_image;
    config.enableTelemetry = true;

    if (const Status s = engine_->initialize(config); !s.ok()) {
        lastError_ = s.message();
        AUREA_LOG_ERROR("falha ao inicializar: %s", lastError_.c_str());
        engine_->shutdown();
        engine_.reset();
        return s;
    }

    // Engine owns (and may destroy) the backend when initialization fails.
    backend = engine_->gpu();
    if (!backend) {
        const auto status = engine_->read_status();
        lastError_ = std::string(to_string(status.lastError)) + ": " + status.lastErrorDetail;
        engine_->shutdown();
        engine_.reset();
        return Status{status.lastError == Errc::Ok ? Errc::NotSupported : status.lastError, lastError_.c_str()};
    }

    // ShaderLibrary prewarms the essential editor pipelines during initialize,
    // but Renderer permits individual failures. An iOS host must not advertise
    // a working preview after those failures: it would only show the clear color.
    const EngineTelemetry startup = engine_->read_telemetry();
    if (startup.shaderFailures != 0 || startup.pipelineCount == 0) {
        lastError_ = "pipelines essenciais Metal indisponiveis ("
            + std::to_string(startup.shaderFailures) + " falhas, "
            + std::to_string(startup.pipelineCount) + " pipelines criados)";
        AUREA_LOG_ERROR("%s", lastError_.c_str());
        engine_->shutdown();
        engine_.reset();
        return Status{Errc::PipelineCompileFailed, lastError_.c_str()};
    }

    // Zero-copy onde a GPU importa o CVPixelBuffer do decoder como textura
    // Metal. No iOS não há o quirk do emulador do Android: ou o backend importa
    // o buffer IOSurface, ou os planos veem pela CPU.
    const bool zeroCopy = backend && backend->capabilities().externalMemoryHardwareBuffer;
    if (mediaControl_) mediaControl_->set_zero_copy(zeroCopy);
    AUREA_LOG_INFO("video: %s", zeroCopy ? "zero-copy (CVPixelBuffer/IOSurface)" : "planos pela CPU");

    engine_->start_render_thread();
    initialized_ = true;
    AUREA_LOG_INFO("motor iOS pronto (v%s)", AUREA_VERSION_LABEL);
    return OkStatus;
}

void Host::shutdown() noexcept {
    if (engine_) {
        engine_->shutdown();   // para a thread de render e devolve os decoders
        engine_.reset();       // o Engine morre ANTES do backend (posse dele)
    }
    hasSurface_ = false;
    initialized_ = false;
    audioOut_.reset();
    media_.reset();
    mediaControl_ = nullptr;
}

GPUBackend* Host::gpu() noexcept {
    return engine_ ? engine_->gpu() : nullptr;
}

bool Host::attach_surface(void* metalLayer, u32 width, u32 height) noexcept {
    if (!engine_ || !metalLayer || width == 0 || height == 0) return false;
    // O layer é emprestado pela view. `SurfaceDesc::nativeWindow` é CAMetalLayer*
    // no iOS (GPUBackend.hpp) — o backend lê o dispositivo e o drawable dele.
    const Status s = engine_->attach_surface(metalLayer, width, height);
    if (!s.ok()) {
        AUREA_LOG_ERROR("superficie recusada: %s", s.message().data());
        return false;
    }
    hasSurface_ = true;
    return true;
}

void Host::detach_surface() noexcept {
    if (!engine_) return;
    // Volta só depois que a GPU largou o layer: a view pode ser destruída logo
    // em seguida (fim da tela, troca de aba). Mesmo contrato do Android.
    engine_->detach_surface();
    hasSurface_ = false;
}

void Host::resize_surface(u32 width, u32 height) noexcept {
    if (!engine_ || width == 0 || height == 0) return;
    (void)engine_->resize_surface(width, height);
}

void Host::request_render() noexcept {
    if (engine_) engine_->request_render();
}

void Host::wake_render() noexcept {
    if (engine_) engine_->wake_render();
}

void Host::invalidate() noexcept {
    if (engine_) engine_->invalidate();
}

} // namespace aurea::ios
