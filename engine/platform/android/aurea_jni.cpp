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

#include "AAudioOutput.hpp"
#include "MediaCodecExport.hpp"
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
jmethodID g_decodeImage = nullptr;       // AureaEngine.decodeImage(String): ByteArray?

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

/// Pede à plataforma os pixels de uma imagem do projeto (reabrir projeto).
/// Resposta: 8 bytes (largura, altura em u32 little-endian) + RGBA8 reto.
bool load_image(const char* source, ImagePixels& out, void*) {
    if (!g_vm || !g_engineClass || !g_decodeImage) return false;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    }
    bool ok = false;
    if (jstring js = env->NewStringUTF(source)) {
        auto arr = static_cast<jbyteArray>(env->CallStaticObjectMethod(g_engineClass, g_decodeImage, js));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            arr = nullptr;
        }
        if (arr) {
            const jsize n = env->GetArrayLength(arr);
            if (n > 8) {
                u8 header[8];
                env->GetByteArrayRegion(arr, 0, 8, reinterpret_cast<jbyte*>(header));
                const u32 w = header[0] | (header[1] << 8) | (header[2] << 16) | (static_cast<u32>(header[3]) << 24);
                const u32 h = header[4] | (header[5] << 8) | (header[6] << 16) | (static_cast<u32>(header[7]) << 24);
                const usize bytes = static_cast<usize>(w) * h * 4;
                if (w && h && static_cast<usize>(n) == bytes + 8) {
                    out.width = w;
                    out.height = h;
                    out.rgba.resize(bytes);
                    env->GetByteArrayRegion(arr, 8, static_cast<jsize>(bytes), reinterpret_cast<jbyte*>(out.rgba.data()));
                    ok = true;
                }
            }
            env->DeleteLocalRef(arr);
        }
        env->DeleteLocalRef(js);
    }
    if (attached) g_vm->DetachCurrentThread();
    return ok;
}

// -----------------------------------------------------------------------------
// Contexto nativo: um por motor
// -----------------------------------------------------------------------------
struct NativeContext {
    android::AAudioOutput audioOut;    ///< antes do motor: o motor fecha a saída no shutdown
    Engine engine;
    android::MediaCodecFactory media;
    std::mutex surfaceMutex;
    ANativeWindow* window = nullptr;   ///< referência adquirida; devolvida no detach
    bool initialized = false;
    scene3d::ImportProgress importProgress;   ///< o import 3D em curso (um por vez)
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
        g_decodeImage = env->GetStaticMethodID(g_engineClass, "decodeImage", "(Ljava/lang/String;)[B");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            g_decodeImage = nullptr;
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
    config.exportSinkFactory = &android::make_mediacodec_export_sink;
    config.audioOutput = &c->audioOut;
    config.defaultFontPath = "/system/fonts/Roboto-Regular.ttf";
    config.imageLoader = &load_image;
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

AUREA_JNI void AUREA_FN(nativeInvalidate)(JNIEnv*, jclass, jlong handle) {
    if (NativeContext* c = ctx_of(handle)) c->engine.invalidate();
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

/// Composição atual: devolve o id; `out` = [largura, altura, fps, duração, r, g, b, a].
AUREA_JNI jlong AUREA_FN(nativeQueryComposition)(JNIEnv* env, jclass, jlong handle, jdoubleArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 8) return 0;
    u64 id = 0;
    u32 w = 0, h = 0;
    f64 fps = 0.0;
    i64 dur = 0;
    f32 bg[4]{};
    if (!c->engine.query_composition(id, w, h, fps, dur, bg)) return 0;
    const jdouble v[8] = {static_cast<jdouble>(w), static_cast<jdouble>(h), fps, static_cast<jdouble>(dur),
                          bg[0], bg[1], bg[2], bg[3]};
    env->SetDoubleArrayRegion(out, 0, 8, v);
    if (env->GetArrayLength(out) >= 10) {
        u32 capLong = 0, capShort = 0;
        c->engine.composition_size_cap(capLong, capShort);
        const jdouble cap[2] = {static_cast<jdouble>(capLong), static_cast<jdouble>(capShort)};
        env->SetDoubleArrayRegion(out, 8, 2, cap);
    }
    return static_cast<jlong>(id);
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

AUREA_JNI jlong AUREA_FN(nativeImportAudio)(JNIEnv* env, jclass, jlong handle, jstring source, jstring name) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    VideoImport request;
    request.sourcePath = to_string(env, source);
    request.displayName = to_string(env, name);
    const Result<u64> r = c->engine.import_audio(request);
    if (!r.ok()) {
        AUREA_LOG_ERROR("importacao de audio falhou: %s", r.status().message().data());
        return -static_cast<jlong>(r.status().code());
    }
    return static_cast<jlong>(*r);
}

/// Waveform: `count` baldes u8 em `out` (buffer direto). Devolve os escritos (0 = sem som).
AUREA_JNI jint AUREA_FN(nativeQueryWaveform)(JNIEnv* env, jclass, jlong handle, jlong layerId, jdouble startFrame,
                                             jdouble framesPerBucket, jint count, jobject out) {
    NativeContext* c = ctx_of(handle);
    if (!c || count <= 0) return 0;
    auto* dst = pod_buffer<u8>(env, out, count);
    if (!dst) return 0;
    return static_cast<jint>(c->engine.query_waveform(static_cast<u64>(layerId), startFrame, framesPerBucket,
                                                      static_cast<u32>(count), dst));
}

AUREA_JNI jlong AUREA_FN(nativeFreezeFrame)(JNIEnv*, jclass, jlong handle, jlong layerId, jint frame, jint holdFrames) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> r = c->engine.freeze_frame(static_cast<u64>(layerId), frame, holdFrames);
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jlong AUREA_FN(nativeAddShape)(JNIEnv*, jclass, jlong handle, jint preset) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> r = c->engine.add_shape(static_cast<u32>(std::max(0, preset)));
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jlong AUREA_FN(nativeAddText)(JNIEnv* env, jclass, jlong handle, jstring content) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const std::string s = to_string(env, content);
    const Result<u64> r = c->engine.add_text(s.c_str());
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

/// Texto da camada: devolve o conteúdo e preenche `out` (10 floats): tamanho,
/// cor RGBA (sRGB), contorno, cor do contorno RGBA... na ordem de TextDetail.kt.
AUREA_JNI jstring AUREA_FN(nativeQueryText)(JNIEnv* env, jclass, jlong handle, jlong layerId, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    TextData t;
    if (!c->engine.query_text(static_cast<u64>(layerId), t)) return nullptr;
    if (out && env->GetArrayLength(out) >= 13) {
        const jfloat v[13] = {t.size, t.color.x, t.color.y, t.color.z, t.color.w, t.strokeWidth,
                              t.strokeColor.x, t.strokeColor.y, t.strokeColor.z, t.strokeColor.w,
                              static_cast<jfloat>(t.alignment), t.lineHeight, t.tracking};
        env->SetFloatArrayRegion(out, 0, 13, v);
    }
    return env->NewStringUTF(t.content.c_str());
}

namespace {
std::vector<u64> jlongs(JNIEnv* env, jlongArray ids) {
    std::vector<u64> v;
    if (!ids) return v;
    v.resize(static_cast<usize>(env->GetArrayLength(ids)));
    if (!v.empty()) env->GetLongArrayRegion(ids, 0, static_cast<jsize>(v.size()), reinterpret_cast<jlong*>(v.data()));
    return v;
}
} // namespace

AUREA_JNI jint AUREA_FN(nativeCopyLayers)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    NativeContext* c = ctx_of(handle);
    const auto v = jlongs(env, ids);
    return c ? static_cast<jint>(c->engine.copy_layers(v.data(), static_cast<u32>(v.size()))) : 0;
}

AUREA_JNI jint AUREA_FN(nativePasteLayers)(JNIEnv*, jclass, jlong handle, jlong frame) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.paste_layers(frame)) : 0;
}

AUREA_JNI jboolean AUREA_FN(nativeCopyStyle)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.copy_style(static_cast<u64>(layer)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativePasteStyle)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    NativeContext* c = ctx_of(handle);
    const auto v = jlongs(env, ids);
    return c ? static_cast<jint>(c->engine.paste_style(v.data(), static_cast<u32>(v.size()))) : 0;
}

AUREA_JNI jint AUREA_FN(nativeCopyEffects)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.copy_effects(static_cast<u64>(layer))) : 0;
}

AUREA_JNI jint AUREA_FN(nativePasteEffects)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    NativeContext* c = ctx_of(handle);
    const auto v = jlongs(env, ids);
    return c ? static_cast<jint>(c->engine.paste_effects(v.data(), static_cast<u32>(v.size()))) : 0;
}

AUREA_JNI jint AUREA_FN(nativeCopyKeyframes)(JNIEnv*, jclass, jlong handle, jlong layer, jlong frame) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.copy_keyframes(static_cast<u64>(layer), frame)) : 0;
}

AUREA_JNI jint AUREA_FN(nativePasteKeyframes)(JNIEnv* env, jclass, jlong handle, jlongArray ids, jlong frame) {
    NativeContext* c = ctx_of(handle);
    const auto v = jlongs(env, ids);
    return c ? static_cast<jint>(c->engine.paste_keyframes(v.data(), static_cast<u32>(v.size()), frame)) : 0;
}

AUREA_JNI jint AUREA_FN(nativeClipboardState)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.clipboard_state()) : 0;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryGizmo)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloat length, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 8) return JNI_FALSE;
    f32 v[8];
    if (!c->engine.query_gizmo(static_cast<u64>(layer), length, v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 8, v);
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeGizmoMoveLocal)(JNIEnv* env, jclass, jlong handle, jlong layer, jint axis, jfloat amount, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 3) return JNI_FALSE;
    f32 v[3];
    if (!c->engine.gizmo_move_local(static_cast<u64>(layer), static_cast<u32>(axis), amount, v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 3, v);
    return JNI_TRUE;
}

AUREA_JNI jlong AUREA_FN(nativeImportHdri)(JNIEnv* env, jclass, jlong handle, jstring path) {
    NativeContext* c = ctx_of(handle);
    if (!c || !path) return -static_cast<jlong>(Errc::InvalidState);
    const char* p = env->GetStringUTFChars(path, nullptr);
    const Result<u64> r = c->engine.import_hdri(p);
    env->ReleaseStringUTFChars(path, p);
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jboolean AUREA_FN(nativeClearHdri)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.clear_hdri() ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetEnvironment)(JNIEnv*, jclass, jlong handle, jfloat intensity, jfloat rotation) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_environment_params(intensity, rotation) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryEnvironment)(JNIEnv* env, jclass, jlong handle, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 3) return JNI_FALSE;
    f32 v[3];
    if (!c->engine.query_environment(v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 3, v);
    return JNI_TRUE;
}

AUREA_JNI jlong AUREA_FN(nativePrecompose)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const auto v = jlongs(env, ids);
    const Result<u64> r = c->engine.precompose(v.data(), static_cast<u32>(v.size()));
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jboolean AUREA_FN(nativeOpenPrecomp)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.open_precomp(static_cast<u64>(layer)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeClosePrecomp)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.close_precomp() ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativePrecompDepth)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.precomp_depth()) : 0;
}

AUREA_JNI jstring AUREA_FN(nativeCompositionName)(JNIEnv* env, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    const std::string n = c ? c->engine.current_composition_name() : std::string{};
    return env->NewStringUTF(n.c_str());
}

AUREA_JNI jboolean AUREA_FN(nativeSetTimeRemap)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_time_remap(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeApplySpeedRamp)(JNIEnv*, jclass, jlong handle, jlong layer, jint preset) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.apply_speed_ramp(static_cast<u64>(layer), static_cast<u32>(preset)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jlong AUREA_FN(nativeTrackPoint)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloat x, jfloat y,
                                           jboolean stabilize, jintArray trackedOut) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    u32 tracked = 0;
    const Result<u64> r = c->engine.track_point(static_cast<u64>(layer), x, y, stabilize == JNI_TRUE, &tracked);
    if (trackedOut && env->GetArrayLength(trackedOut) > 0) {
        const jint t = static_cast<jint>(tracked);
        env->SetIntArrayRegion(trackedOut, 0, 1, &t);
    }
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jboolean AUREA_FN(nativeSetEcho)(JNIEnv*, jclass, jlong handle, jlong layer, jint count, jfloat delay, jfloat decay) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_echo(static_cast<u64>(layer), static_cast<u32>(std::max(0, count)), delay, decay) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetRgbTime)(JNIEnv*, jclass, jlong handle, jlong layer, jfloat delay) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_rgb_time(static_cast<u64>(layer), delay) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryEcho)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 4) return JNI_FALSE;
    f32 v[4];
    if (!c->engine.query_echo(static_cast<u64>(layer), v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 4, v);
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetTransition)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean out, jint type, jint frames) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_transition(static_cast<u64>(layer), out == JNI_TRUE, static_cast<u32>(type),
                                         static_cast<u32>(std::max(0, frames))) ? JNI_TRUE : JNI_FALSE;
}

namespace {
aurea::scene3d::Text3DSpec text3d_spec(JNIEnv* env, jstring content, jfloat depth, jint align, jfloat r, jfloat g, jfloat b) {
    aurea::scene3d::Text3DSpec s;
    const char* p = env->GetStringUTFChars(content, nullptr);
    s.content = p;
    env->ReleaseStringUTFChars(content, p);
    s.depth = depth;
    s.alignment = static_cast<u32>(align);
    s.color = Vec4{r, g, b, 1.0f};
    return s;
}
} // namespace

AUREA_JNI jlong AUREA_FN(nativeAddText3d)(JNIEnv* env, jclass, jlong handle, jstring content, jfloat depth, jint align,
                                         jfloat r, jfloat g, jfloat b) {
    NativeContext* c = ctx_of(handle);
    if (!c || !content) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> res = c->engine.add_text3d(text3d_spec(env, content, depth, align, r, g, b));
    if (!res.ok()) return -static_cast<jlong>(res.status().code());
    return static_cast<jlong>(*res);
}

AUREA_JNI jboolean AUREA_FN(nativeSetText3d)(JNIEnv* env, jclass, jlong handle, jlong layer, jstring content, jfloat depth,
                                            jint align, jfloat r, jfloat g, jfloat b) {
    NativeContext* c = ctx_of(handle);
    if (!c || !content) return JNI_FALSE;
    return c->engine.set_text3d(static_cast<u64>(layer), text3d_spec(env, content, depth, align, r, g, b)).ok() ? JNI_TRUE : JNI_FALSE;
}

/// Receita do texto 3D: devolve o texto (nulo = não é texto 3D) e preenche
/// {profundidade, alinhamento, r, g, b}.
AUREA_JNI jstring AUREA_FN(nativeQueryText3d)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 5) return nullptr;
    aurea::scene3d::Text3DSpec s;
    if (!c->engine.query_text3d(static_cast<u64>(layer), s)) return nullptr;
    const f32 v[5] = {s.depth, static_cast<f32>(s.alignment), s.color.x, s.color.y, s.color.z};
    env->SetFloatArrayRegion(out, 0, 5, v);
    return env->NewStringUTF(s.content.c_str());
}

AUREA_JNI jlong AUREA_FN(nativeAddParticles)(JNIEnv*, jclass, jlong handle, jint preset) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> r = c->engine.add_particles(static_cast<u32>(preset));
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jboolean AUREA_FN(nativeApplyParticlePreset)(JNIEnv*, jclass, jlong handle, jlong layer, jint preset) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.apply_particle_preset(static_cast<u64>(layer), static_cast<u32>(preset)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetParticleParam)(JNIEnv*, jclass, jlong handle, jlong layer, jint param, jfloat value) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_particle_param(static_cast<u64>(layer), static_cast<u32>(param), value) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryParticles)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 8) return JNI_FALSE;
    f32 v[8];
    if (!c->engine.query_particles(static_cast<u64>(layer), v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 8, v);
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetMotionBlur)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_motion_blur(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI void AUREA_FN(nativeSetCompositionMotionBlur)(JNIEnv*, jclass, jlong handle, jboolean on) {
    if (NativeContext* c = ctx_of(handle)) c->engine.set_composition_motion_blur(on == JNI_TRUE);
}

AUREA_JNI void AUREA_FN(nativeSetShutterAngle)(JNIEnv*, jclass, jlong handle, jfloat degrees) {
    if (NativeContext* c = ctx_of(handle)) c->engine.set_shutter_angle(degrees);
}

AUREA_JNI jfloat AUREA_FN(nativeMotionBlurState)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    bool on = false;
    f32 shutter = 180.0f;
    if (!c || !c->engine.query_motion_blur(on, shutter)) return 0.0f;
    return on ? shutter + 1.0f : -(shutter + 1.0f);   // sinal = ligado; módulo − 1 = obturador
}

AUREA_JNI void AUREA_FN(nativeSetEditMode)(JNIEnv*, jclass, jlong handle, jboolean on) {
    if (NativeContext* c = ctx_of(handle)) c->engine.set_edit_mode(on == JNI_TRUE);
}

AUREA_JNI jboolean AUREA_FN(nativeEditMode)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.edit_mode() ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeRippleDelete)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    NativeContext* c = ctx_of(handle);
    if (!c || !ids) return JNI_FALSE;
    const jsize n = env->GetArrayLength(ids);
    std::vector<u64> v(static_cast<usize>(n));
    env->GetLongArrayRegion(ids, 0, n, reinterpret_cast<jlong*>(v.data()));
    return c->engine.ripple_delete(v.data(), static_cast<u32>(n)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jlong AUREA_FN(nativeRemoveGaps)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jlong>(c->engine.remove_gaps()) : 0;
}

AUREA_JNI jboolean AUREA_FN(nativeTrimComposition)(JNIEnv*, jclass, jlong handle, jlong frame) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.trim_composition(frame) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeToggleMarker)(JNIEnv*, jclass, jlong handle, jlong frame) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.toggle_marker(frame) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeMoveMarker)(JNIEnv*, jclass, jlong handle, jlong from, jlong to) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.move_marker(from, to) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeQueryMarkers)(JNIEnv* env, jclass, jlong handle, jlongArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out) return 0;
    const jsize len = env->GetArrayLength(out);
    std::vector<i64> tmp(static_cast<usize>(len));
    const u32 total = c->engine.query_markers(tmp.data(), static_cast<u32>(len / 3));
    const jsize copy = std::min<jsize>(len, static_cast<jsize>(std::min<u32>(total, static_cast<u32>(len / 3)) * 3));
    if (copy > 0) env->SetLongArrayRegion(out, 0, copy, reinterpret_cast<const jlong*>(tmp.data()));
    return static_cast<jint>(total);
}

AUREA_JNI jlong AUREA_FN(nativeDetectBeats)(JNIEnv* env, jclass, jlong handle, jlong layerId, jdoubleArray bpmOut) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    f64 bpm = 0.0;
    const Result<u32> r = c->engine.detect_beats(static_cast<u64>(layerId), &bpm);
    if (bpmOut && env->GetArrayLength(bpmOut) > 0) env->SetDoubleArrayRegion(bpmOut, 0, 1, &bpm);
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jlong AUREA_FN(nativeAddNull)(JNIEnv*, jclass, jlong handle, jboolean threeD) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> r = c->engine.add_null(threeD == JNI_TRUE);
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jlong AUREA_FN(nativeExtractAudio)(JNIEnv*, jclass, jlong handle, jlong layerId) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> r = c->engine.extract_audio(static_cast<u64>(layerId));
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jlong AUREA_FN(nativeImportImage)(JNIEnv* env, jclass, jlong handle, jobject rgba, jint width,
                                            jint height, jstring name, jstring source) {
    NativeContext* c = ctx_of(handle);
    if (!c || width <= 0 || height <= 0) return -static_cast<jlong>(Errc::InvalidArgument);
    const jlong bytes = static_cast<jlong>(width) * height * 4;
    const auto* pixels = pod_buffer<const u8>(env, rgba, bytes);
    if (!pixels) return -static_cast<jlong>(Errc::InvalidArgument);
    const std::string n = to_string(env, name);
    const std::string src = to_string(env, source);
    const Result<u64> r = c->engine.import_image(pixels, static_cast<u32>(width), static_cast<u32>(height), n.c_str(),
                                                 src.empty() ? nullptr : src.c_str());
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

// =============================================================================
// Modelo 3D (glTF/GLB). Chamado numa thread de fundo; progresso e cancelamento
// pelas duas funções abaixo, de qualquer thread.
// =============================================================================
AUREA_JNI jlong AUREA_FN(nativeImportModel)(JNIEnv* env, jclass, jlong handle, jstring path, jstring name,
                                            jobjectArray detailOut) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    ModelImport req;
    req.path = to_string(env, path);
    req.displayName = to_string(env, name);
    c->importProgress.cancel.store(false);
    c->importProgress.phase.store(scene3d::ImportPhase::Queued);
    c->importProgress.fraction.store(0.0f);
    std::string detail;
    const Result<u64> r = c->engine.import_model(req, &c->importProgress, &detail);
    if (detailOut && env->GetArrayLength(detailOut) > 0) {
        jstring d = env->NewStringUTF(detail.c_str());
        env->SetObjectArrayElement(detailOut, 0, d);
        env->DeleteLocalRef(d);
    }
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

/// Etapa (ImportPhase) × 1000 + fração × 1000 da etapa.
AUREA_JNI jint AUREA_FN(nativeImportModelProgress)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return 0;
    const u32 phase = static_cast<u32>(c->importProgress.phase.load());
    const f32 f = std::clamp(c->importProgress.fraction.load(), 0.0f, 0.999f);
    return static_cast<jint>(phase * 1000 + static_cast<u32>(f * 1000.0f));
}

AUREA_JNI void AUREA_FN(nativeCancelModelImport)(JNIEnv*, jclass, jlong handle) {
    if (NativeContext* c = ctx_of(handle)) c->importProgress.cancel.store(true);
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
/// `shortSide` = lado menor do vídeo (720/1080/1440/2160); `fps` 0 = o da
/// composição; `codec` 0 = H.264, 1 = HEVC; `bitrateMbps` 0 = automático.
AUREA_JNI jint AUREA_FN(nativeStartExport)(JNIEnv* env, jclass, jlong handle, jstring outputPath, jint shortSide,
                                           jdouble fps, jint codec, jint bitrateMbps) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    const std::string p = to_string(env, outputPath);
    ExportSettings settings;
    settings.width = 0;
    settings.height = shortSide > 0 ? static_cast<u32>(shortSide) : 0;
    settings.fps = fps > 0.0 ? fps : 0.0;
    settings.videoCodec = codec == 1 ? ExportCodec::HEVC : ExportCodec::H264;
    settings.videoBitrateMbps = bitrateMbps > 0 ? static_cast<u32>(bitrateMbps) : 0;
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
