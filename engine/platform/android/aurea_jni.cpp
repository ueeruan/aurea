// =============================================================================
//  Aurea / platform / android / aurea_jni.cpp
//
//  A ponte entre Kotlin e o motor C++.
//
//  Este arquivo é BURRO de propósito: traduz tipos e chama o motor. Toda regra
//  vive no C++ compartilhado, onde o iOS também a executa.
//
//  O que atravessa:
//   - comandos (UI → motor): um bloco contíguo de structs POD por lote;
//   - status/perf (motor → UI): structs POD escritos em buffers diretos;
//   - consultas (layers, efeitos, parâmetros): linhas POD + blob de texto.
//
//  NENHUM BITMAP ATRAVESSA. O preview vai do renderer direto para o
//  ANativeWindow do SurfaceView, pela thread de render do próprio motor.
// =============================================================================
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <sys/system_properties.h>

#include "MediaCodecSource.hpp"
#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Version.hpp"

#include <cstring>
#include <mutex>
#include <new>
#include <string>

using namespace aurea;

namespace {

// -----------------------------------------------------------------------------
// JVM
// -----------------------------------------------------------------------------
JavaVM* g_vm = nullptr;
jclass g_engineClass = nullptr;          // referência global: FindClass numa thread
jmethodID g_openContentFd = nullptr;     // nativa usaria o class loader do sistema

void android_log_sink(LogLevel level, const char* message, void*) {
    int prio = ANDROID_LOG_INFO;
    switch (level) {
        case LogLevel::Trace: prio = ANDROID_LOG_VERBOSE; break;
        case LogLevel::Debug: prio = ANDROID_LOG_DEBUG;   break;
        case LogLevel::Info:  prio = ANDROID_LOG_INFO;    break;
        case LogLevel::Warn:  prio = ANDROID_LOG_WARN;    break;
        case LogLevel::Error: prio = ANDROID_LOG_ERROR;   break;
        case LogLevel::Fatal: prio = ANDROID_LOG_FATAL;   break;
    }
    __android_log_write(prio, "AureaEngine", message);
}

/// Abre uma URI content:// pelo ContentResolver (lado Kotlin) e devolve um
/// descritor que passa a ser nosso. Chamado das threads do motor (sondagem,
/// abertura de decoder), que podem não estar presas à JVM.
int open_content_fd(const char* uri, void*) {
    if (!g_vm || !g_engineClass || !g_openContentFd) return -1;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return -1;
        attached = true;
    }
    int fd = -1;
    jstring juri = env->NewStringUTF(uri);
    if (juri) {
        fd = env->CallStaticIntMethod(g_engineClass, g_openContentFd, juri);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            fd = -1;
        }
        env->DeleteLocalRef(juri);
    }
    // A thread nativa precisa se soltar antes de terminar, ou a ART aborta.
    if (attached) g_vm->DetachCurrentThread();
    return fd;
}

// -----------------------------------------------------------------------------
// Contexto nativo: um por motor
// -----------------------------------------------------------------------------
struct NativeContext {
    Engine engine;
    android::MediaCodecFactory media;
    std::mutex surfaceMutex;
    ANativeWindow* window = nullptr;   ///< referência adquirida; devolvida no detach
    bool initialized = false;
};

[[nodiscard]] NativeContext* ctx_of(jlong handle) noexcept {
    return handle ? reinterpret_cast<NativeContext*>(static_cast<intptr_t>(handle)) : nullptr;
}

[[nodiscard]] void* buffer_ptr(JNIEnv* env, jobject buffer) noexcept {
    return buffer ? env->GetDirectBufferAddress(buffer) : nullptr;
}

[[nodiscard]] jlong buffer_capacity(JNIEnv* env, jobject buffer) noexcept {
    return buffer ? env->GetDirectBufferCapacity(buffer) : 0;
}

/// Buffer direto com pelo menos `bytes`, ou nullptr.
template <typename T>
[[nodiscard]] T* pod_buffer(JNIEnv* env, jobject buffer, jlong bytes) noexcept {
    void* p = buffer_ptr(env, buffer);
    if (!p || buffer_capacity(env, buffer) < bytes) return nullptr;
    return static_cast<T*>(p);
}

/// Linhas que cabem no buffer, limitadas ao pedido.
template <typename Row>
[[nodiscard]] u32 row_capacity(JNIEnv* env, jobject buffer, jint requested) noexcept {
    if (requested <= 0) return 0;
    const jlong fit = buffer_capacity(env, buffer) / static_cast<jlong>(sizeof(Row));
    return static_cast<u32>(std::min<jlong>(fit, requested));
}

[[nodiscard]] std::string to_string(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    if (!chars) return {};
    std::string result(chars);
    env->ReleaseStringUTFChars(s, chars);
    return result;
}

/// Emulador do Android Studio (goldfish/ranchu com gfxstream).
[[nodiscard]] bool running_on_emulator() noexcept {
    char value[PROP_VALUE_MAX]{};
    if (__system_property_get("ro.boot.qemu", value) > 0 && value[0] == '1') return true;
    if (__system_property_get("ro.kernel.qemu", value) > 0 && value[0] == '1') return true;
    if (__system_property_get("ro.hardware", value) > 0
        && (std::strcmp(value, "ranchu") == 0 || std::strcmp(value, "goldfish") == 0)) {
        return true;
    }
    return false;
}

void release_window_locked(NativeContext& c) noexcept {
    if (c.window) {
        ANativeWindow_release(c.window);
        c.window = nullptr;
    }
}

} // namespace

#define AUREA_JNI extern "C" JNIEXPORT
#define AUREA_FN(name) JNICALL Java_com_aurea_aurea_engine_AureaEngine_##name

AUREA_JNI jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    set_log_sink(&android_log_sink, nullptr);
    if (jclass local = env->FindClass("com/aurea/aurea/engine/AureaEngine")) {
        g_engineClass = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        g_openContentFd = env->GetStaticMethodID(g_engineClass, "openContentFd", "(Ljava/lang/String;)I");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            g_openContentFd = nullptr;
        }
    }
    return JNI_VERSION_1_6;
}

// =============================================================================
// Ciclo de vida
// =============================================================================
AUREA_JNI jlong AUREA_FN(nativeCreate)(JNIEnv*, jclass) {
    auto* c = new (std::nothrow) NativeContext();
    if (!c) {
        AUREA_LOG_FATAL("nao foi possivel alocar o motor");
        return 0;
    }
    c->media.set_fd_opener(&open_content_fd, nullptr);
    AUREA_LOG_INFO("motor criado (v%s)", AUREA_VERSION_LABEL);
    return static_cast<jlong>(reinterpret_cast<intptr_t>(c));
}

AUREA_JNI void AUREA_FN(nativeDestroy)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return;
    c->engine.shutdown();
    {
        std::lock_guard<std::mutex> lock(c->surfaceMutex);
        release_window_locked(*c);
    }
    delete c;
    AUREA_LOG_INFO("motor destruido");
}

/// Inicializa o motor SEM superfície: instância e dispositivo Vulkan, renderer,
/// pipelines e a thread de render. A superfície chega depois, pelo SurfaceView.
AUREA_JNI jboolean AUREA_FN(nativeInitialize)(JNIEnv* env, jclass, jlong handle, jfloat refreshRate,
                                              jstring cacheDir, jstring documentsDir, jboolean debug) {
    NativeContext* c = ctx_of(handle);
    if (!c) return JNI_FALSE;
    if (c->initialized) return JNI_TRUE;

    EngineConfig config;
    config.backend = new (std::nothrow) vk::Backend();
    config.backendConfig.enableValidation = debug == JNI_TRUE;
    config.backendConfig.enableGpuTimers = true;
    config.cacheDirectory = to_string(env, cacheDir);
    config.documentsDirectory = to_string(env, documentsDir);
    config.displayRefreshRate = refreshRate > 0.0f ? refreshRate : 60.0f;
    config.mediaFactory = &c->media;
    config.enableTelemetry = true;

    if (const Status s = c->engine.initialize(config); !s.ok()) {
        AUREA_LOG_ERROR("falha ao inicializar: %s", s.message().data());
        return JNI_FALSE;
    }
    GPUBackend* gpu = c->engine.gpu();
    if (!gpu) {
        AUREA_LOG_ERROR("sem GPU utilizavel: o preview nao tem como desenhar");
        c->engine.shutdown();
        return JNI_FALSE;
    }
    // Zero-copy só onde a GPU importa o AHardwareBuffer do decoder; senão os
    // planos vêm pela CPU. Decidido uma vez, a partir das capacidades reais.
    //
    // QUIRK: o emulador (gfxstream) anuncia VK_ANDROID_external_memory_android_
    // hardware_buffer e a conversão YCbCr, mas amostra o buffer YUV externo como
    // bytes crus (plano Y em cima, UV embaixo). Lá o caminho é o de planos.
    const bool emulator = running_on_emulator();
    const bool zeroCopy = gpu->capabilities().zero_copy_video() && !emulator;
    c->media.set_zero_copy(zeroCopy);
    AUREA_LOG_INFO("video: %s%s", zeroCopy ? "zero-copy (AHardwareBuffer)" : "planos pela CPU",
                   emulator ? " (emulador: YCbCr externo nao confiavel)" : "");
    c->engine.start_render_thread();
    c->initialized = true;
    return JNI_TRUE;
}

AUREA_JNI void AUREA_FN(nativeShutdown)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return;
    c->engine.shutdown();
    c->initialized = false;
    std::lock_guard<std::mutex> lock(c->surfaceMutex);
    release_window_locked(*c);
}

AUREA_JNI void AUREA_FN(nativeSuspend)(JNIEnv*, jclass, jlong handle) {
    if (NativeContext* c = ctx_of(handle)) (void)c->engine.suspend();
}

AUREA_JNI void AUREA_FN(nativeResume)(JNIEnv*, jclass, jlong handle) {
    if (NativeContext* c = ctx_of(handle)) (void)c->engine.resume();
}

// =============================================================================
// Superfície
// =============================================================================
AUREA_JNI jboolean AUREA_FN(nativeAttachSurface)(JNIEnv* env, jclass, jlong handle, jobject surface,
                                                 jint width, jint height) {
    NativeContext* c = ctx_of(handle);
    if (!c || !surface) return JNI_FALSE;
    // `fromSurface` adquire uma referência: guardada até o detach. Sem ela o
    // Vulkan desenharia numa janela que o sistema já destruiu.
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return JNI_FALSE;
    std::lock_guard<std::mutex> lock(c->surfaceMutex);
    const Status s = c->engine.attach_surface(window, static_cast<u32>(width), static_cast<u32>(height));
    if (!s.ok()) {
        AUREA_LOG_ERROR("superficie recusada: %s", s.message().data());
        ANativeWindow_release(window);
        return JNI_FALSE;
    }
    release_window_locked(*c);
    c->window = window;
    return JNI_TRUE;
}

/// Volta só depois que a GPU largou a janela: o Android a destrói em seguida.
AUREA_JNI void AUREA_FN(nativeDetachSurface)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return;
    std::lock_guard<std::mutex> lock(c->surfaceMutex);
    c->engine.detach_surface();
    release_window_locked(*c);
}

AUREA_JNI void AUREA_FN(nativeResizeSurface)(JNIEnv*, jclass, jlong handle, jint width, jint height) {
    if (NativeContext* c = ctx_of(handle)) {
        (void)c->engine.resize_surface(static_cast<u32>(width), static_cast<u32>(height));
    }
}

// =============================================================================
// Comandos e estado
// =============================================================================
AUREA_JNI jint AUREA_FN(nativeSubmitCommands)(JNIEnv* env, jclass, jlong handle, jobject commandBuffer,
                                              jint count, jobject stringBlob, jint stringBlobSize) {
    NativeContext* c = ctx_of(handle);
    if (!c || count <= 0) return 0;
    const jlong need = static_cast<jlong>(count) * static_cast<jlong>(sizeof(Command));
    auto* commands = pod_buffer<const Command>(env, commandBuffer, need);
    if (!commands) {
        AUREA_LOG_ERROR("lote de comandos maior que o buffer (%d)", static_cast<int>(count));
        return 0;
    }
    const auto* blob = static_cast<const char*>(buffer_ptr(env, stringBlob));
    u32 blobSize = 0;
    if (blob && stringBlobSize > 0) {
        blobSize = static_cast<u32>(std::min<jlong>(stringBlobSize, buffer_capacity(env, stringBlob)));
    }
    const u32 accepted = c->engine.submit_commands(commands, static_cast<u32>(count), blob, blobSize);
    c->engine.request_render();
    return static_cast<jint>(accepted);
}

AUREA_JNI jboolean AUREA_FN(nativeReadStatus)(JNIEnv* env, jclass, jlong handle, jobject out) {
    NativeContext* c = ctx_of(handle);
    auto* pod = pod_buffer<bridge::EngineStatusPOD>(env, out, sizeof(bridge::EngineStatusPOD));
    if (!c || !pod) return JNI_FALSE;
    c->engine.fill_status(*pod);
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeReadTelemetry)(JNIEnv* env, jclass, jlong handle, jobject out) {
    NativeContext* c = ctx_of(handle);
    auto* pod = pod_buffer<bridge::TelemetryPOD>(env, out, sizeof(bridge::TelemetryPOD));
    if (!c || !pod) return JNI_FALSE;
    c->engine.fill_telemetry(*pod);
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeReadPerf)(JNIEnv* env, jclass, jlong handle, jobject out) {
    NativeContext* c = ctx_of(handle);
    auto* pod = pod_buffer<bridge::PerfPOD>(env, out, sizeof(bridge::PerfPOD));
    if (!c || !pod) return JNI_FALSE;
    c->engine.fill_perf(*pod);
    return JNI_TRUE;
}

// =============================================================================
// Consultas
// =============================================================================
AUREA_JNI jint AUREA_FN(nativeQueryLayers)(JNIEnv* env, jclass, jlong handle, jobject rows, jint capacity,
                                           jobject blob, jint blobCapacity) {
    NativeContext* c = ctx_of(handle);
    auto* out = static_cast<bridge::LayerRow*>(buffer_ptr(env, rows));
    if (!c || !out) return 0;
    const u32 cap = row_capacity<bridge::LayerRow>(env, rows, capacity);
    const u32 blobCap = static_cast<u32>(std::min<jlong>(std::max(0, blobCapacity), buffer_capacity(env, blob)));
    return static_cast<jint>(c->engine.query_layers(out, cap, static_cast<char*>(buffer_ptr(env, blob)), blobCap));
}

AUREA_JNI jint AUREA_FN(nativeQueryKeyframes)(JNIEnv* env, jclass, jlong handle, jlong layer, jobject rows,
                                              jint capacity) {
    NativeContext* c = ctx_of(handle);
    auto* out = static_cast<bridge::KeyframeRow*>(buffer_ptr(env, rows));
    if (!c || !out) return 0;
    return static_cast<jint>(c->engine.query_keyframes(static_cast<u64>(layer), out,
                                                       row_capacity<bridge::KeyframeRow>(env, rows, capacity)));
}

AUREA_JNI jint AUREA_FN(nativeQueryCurve)(JNIEnv* env, jclass, jlong handle, jlong layer, jint property,
                                          jint from, jint to, jfloatArray out, jint count) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || count <= 0) return 0;
    const jsize len = env->GetArrayLength(out);
    jfloat* values = env->GetFloatArrayElements(out, nullptr);
    if (!values) return 0;
    const u32 written = c->engine.query_curve(static_cast<u64>(layer), static_cast<u32>(property), from, to,
                                              reinterpret_cast<f32*>(values),
                                              static_cast<u32>(std::min<jint>(count, len)));
    env->ReleaseFloatArrayElements(out, values, 0);
    return static_cast<jint>(written);
}

AUREA_JNI jint AUREA_FN(nativeQueryEffectCatalog)(JNIEnv* env, jclass, jlong handle, jobject rows, jint capacity,
                                                  jobject blob) {
    NativeContext* c = ctx_of(handle);
    auto* out = static_cast<bridge::EffectCatalogRow*>(buffer_ptr(env, rows));
    auto* text = static_cast<char*>(buffer_ptr(env, blob));
    if (!c || !out || !text) return 0;
    return static_cast<jint>(c->engine.query_effect_catalog(
        out, row_capacity<bridge::EffectCatalogRow>(env, rows, capacity), text,
        static_cast<u32>(buffer_capacity(env, blob))));
}

AUREA_JNI jint AUREA_FN(nativeQueryLayerEffects)(JNIEnv* env, jclass, jlong handle, jlong layer, jobject rows,
                                                 jint capacity, jobject blob) {
    NativeContext* c = ctx_of(handle);
    auto* out = static_cast<bridge::LayerEffectRow*>(buffer_ptr(env, rows));
    auto* text = static_cast<char*>(buffer_ptr(env, blob));
    if (!c || !out || !text) return 0;
    return static_cast<jint>(c->engine.query_layer_effects(
        static_cast<u64>(layer), out, row_capacity<bridge::LayerEffectRow>(env, rows, capacity), text,
        static_cast<u32>(buffer_capacity(env, blob))));
}

AUREA_JNI jint AUREA_FN(nativeQueryEffectParams)(JNIEnv* env, jclass, jlong handle, jlong layer, jint effectId,
                                                 jobject rows, jint capacity, jobject blob) {
    NativeContext* c = ctx_of(handle);
    auto* out = static_cast<bridge::EffectParamRow*>(buffer_ptr(env, rows));
    auto* text = static_cast<char*>(buffer_ptr(env, blob));
    if (!c || !out || !text) return 0;
    return static_cast<jint>(c->engine.query_effect_params(
        static_cast<u64>(layer), static_cast<u32>(effectId), out,
        row_capacity<bridge::EffectParamRow>(env, rows, capacity), text,
        static_cast<u32>(buffer_capacity(env, blob))));
}

AUREA_JNI jboolean AUREA_FN(nativeQueryLayerDetail)(JNIEnv* env, jclass, jlong handle, jlong layer, jobject out) {
    NativeContext* c = ctx_of(handle);
    auto* pod = pod_buffer<bridge::LayerDetailPOD>(env, out, sizeof(bridge::LayerDetailPOD));
    if (!c || !pod) return JNI_FALSE;
    return c->engine.query_layer_detail(static_cast<u64>(layer), *pod) ? JNI_TRUE : JNI_FALSE;
}

/// Miniatura RGBA8 em `out`; devolve os bytes (0 = ainda na fila). A largura
/// vai em `outWidth[0]`.
AUREA_JNI jint AUREA_FN(nativeQueryThumbnail)(JNIEnv* env, jclass, jlong handle, jlong layer, jint frame,
                                              jint height, jobject out, jintArray outWidth) {
    NativeContext* c = ctx_of(handle);
    auto* dst = static_cast<u8*>(buffer_ptr(env, out));
    if (!c || !dst || height <= 0) return 0;
    u32 width = 0;
    const u32 bytes = c->engine.query_thumbnail(static_cast<u64>(layer), frame, static_cast<u32>(height), dst,
                                                static_cast<u32>(buffer_capacity(env, out)), &width);
    if (bytes && outWidth && env->GetArrayLength(outWidth) > 0) {
        const jint w = static_cast<jint>(width);
        env->SetIntArrayRegion(outWidth, 0, 1, &w);
    }
    return static_cast<jint>(bytes);
}

/// Frame do playhead em RGBA8 sRGB (miniatura do projeto). Devolve os bytes;
/// largura e altura em `outSize[0..1]`.
AUREA_JNI jint AUREA_FN(nativeCaptureFrame)(JNIEnv* env, jclass, jlong handle, jint maxDim, jobject out,
                                            jintArray outSize) {
    NativeContext* c = ctx_of(handle);
    auto* dst = static_cast<u8*>(buffer_ptr(env, out));
    if (!c || !dst || maxDim <= 0) return 0;
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    if (!c->engine.capture_frame_rgba(static_cast<u32>(maxDim), rgba, w, h).ok()) return 0;
    if (static_cast<jlong>(rgba.size()) > buffer_capacity(env, out)) return 0;
    std::memcpy(dst, rgba.data(), rgba.size());
    if (outSize && env->GetArrayLength(outSize) >= 2) {
        const jint wh[2] = {static_cast<jint>(w), static_cast<jint>(h)};
        env->SetIntArrayRegion(outSize, 0, 2, wh);
    }
    return static_cast<jint>(rgba.size());
}

AUREA_JNI void AUREA_FN(nativeSetSelection)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    NativeContext* c = ctx_of(handle);
    if (!c) return;
    const jsize count = ids ? env->GetArrayLength(ids) : 0;
    if (count == 0) {
        c->engine.clear_selection();
        return;
    }
    jlong* raw = env->GetLongArrayElements(ids, nullptr);
    if (!raw) return;
    static_assert(sizeof(jlong) == sizeof(u64));
    c->engine.set_selection(reinterpret_cast<const u64*>(raw), static_cast<u32>(count));
    env->ReleaseLongArrayElements(ids, raw, JNI_ABORT);
    c->engine.request_render();
}

AUREA_JNI void AUREA_FN(nativeClearSelection)(JNIEnv*, jclass, jlong handle) {
    if (NativeContext* c = ctx_of(handle)) c->engine.clear_selection();
}

// =============================================================================
// Importação
// =============================================================================
/// Devolve o id da layer criada (≥ 0) ou `-Errc` em caso de falha.
AUREA_JNI jlong AUREA_FN(nativeImportVideo)(JNIEnv* env, jclass, jlong handle, jstring source, jstring name) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    VideoImport request;
    request.sourcePath = to_string(env, source);
    request.displayName = to_string(env, name);
    const Result<u64> r = c->engine.import_video(request);
    if (!r.ok()) {
        AUREA_LOG_ERROR("importacao de video falhou: %s", r.status().message().data());
        return -static_cast<jlong>(r.status().code());
    }
    return static_cast<jlong>(*r);
}

AUREA_JNI jlong AUREA_FN(nativeImportImage)(JNIEnv* env, jclass, jlong handle, jobject rgba, jint width,
                                            jint height, jstring name) {
    NativeContext* c = ctx_of(handle);
    if (!c || width <= 0 || height <= 0) return -static_cast<jlong>(Errc::InvalidArgument);
    const jlong bytes = static_cast<jlong>(width) * height * 4;
    const auto* pixels = pod_buffer<const u8>(env, rgba, bytes);
    if (!pixels) return -static_cast<jlong>(Errc::InvalidArgument);
    const std::string n = to_string(env, name);
    const Result<u64> r = c->engine.import_image(pixels, static_cast<u32>(width), static_cast<u32>(height), n.c_str());
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

// =============================================================================
// Projeto
// =============================================================================
AUREA_JNI jboolean AUREA_FN(nativeNewProject)(JNIEnv* env, jclass, jlong handle, jint width, jint height,
                                              jfloat fps, jstring title) {
    NativeContext* c = ctx_of(handle);
    if (!c) return JNI_FALSE;
    const std::string t = to_string(env, title);
    return c->engine.new_project(static_cast<u32>(width), static_cast<u32>(height), static_cast<f64>(fps),
                                 t.c_str()).ok() ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeLoadProject)(JNIEnv* env, jclass, jlong handle, jstring path) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    const std::string p = to_string(env, path);
    return static_cast<jint>(c->engine.load_project(p.c_str()).raw());
}

AUREA_JNI jint AUREA_FN(nativeSaveProject)(JNIEnv* env, jclass, jlong handle, jstring path) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    const std::string p = to_string(env, path);
    return static_cast<jint>(c->engine.save_project(p.c_str()).raw());
}

AUREA_JNI jint AUREA_FN(nativeDiscardRecovery)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    c->engine.discard_recovery();
    return static_cast<jint>(Errc::Ok);
}

AUREA_JNI jint AUREA_FN(nativeRecoverSession)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    return static_cast<jint>(c->engine.recover_session().raw());
}

// =============================================================================
// Export (próxima fase: reusa o mesmo renderer)
// =============================================================================
AUREA_JNI jint AUREA_FN(nativeStartExport)(JNIEnv* env, jclass, jlong handle, jstring outputPath) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    const std::string p = to_string(env, outputPath);
    const ExportSettings settings;
    return static_cast<jint>(c->engine.start_export(settings, p.c_str()).raw());
}

AUREA_JNI jint AUREA_FN(nativeCancelExport)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    return static_cast<jint>(c->engine.cancel_export().raw());
}

AUREA_JNI jboolean AUREA_FN(nativeExportProgress)(JNIEnv* env, jclass, jlong handle, jobject out) {
    NativeContext* c = ctx_of(handle);
    auto* pod = pod_buffer<bridge::ExportProgressPOD>(env, out, sizeof(bridge::ExportProgressPOD));
    if (!c || !pod) return JNI_FALSE;
    c->engine.fill_export_progress(*pod);
    return JNI_TRUE;
}
