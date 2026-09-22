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

#include <cstdio>
#include <algorithm>
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

/// Lê a sondagem do aparelho que o Kotlin mandou.
///
/// `probe` são os fatos medidos: [memória total, memória disponível]. Os
/// núcleos NÃO vêm por aqui de propósito: quem os mede é o próprio motor, lendo
/// `/sys/devices/system/cpu/*/cpufreq` — a mesma leitura, uma implementação só,
/// e o iOS usa a mesma. O que só o Android sabe é o limite de memória do
/// processo, e é isso que atravessa.
void read_probe(const jlong* probe, jsize len, PlatformInfo& info) noexcept {
    if (!probe || len < 2) return;
    info.totalMemoryBytes     = static_cast<u64>(probe[0]);
    info.availableMemoryBytes = static_cast<u64>(probe[1]);
}

/// Lê a tabela de codecs do MediaCodecList.
///
/// Sete `int` por linha: tag, é_encoder, hardware, largura, altura, bits,
/// instâncias simultâneas. Uma linha com tag 0 é o fim. O tag é o que liga a
/// linha ao decoder certo — quem manda não precisa saber a ordem da tabela.
void read_codecs(const jint* rows, jsize len, PlatformInfo& info) noexcept {
    if (!rows) return;
    const jsize entry = 7;
    for (jsize at = 0; at + entry <= len; at += entry) {
        const u32 tag = static_cast<u32>(rows[at]);
        if (tag == 0) break;
        const bool encoder = rows[at + 1] != 0;

        CodecCapability cap;
        cap.supported = true;
        cap.hardwareAccelerated = rows[at + 2] != 0;
        cap.maxWidth  = static_cast<u32>(rows[at + 3] > 0 ? rows[at + 3] : 0);
        cap.maxHeight = static_cast<u32>(rows[at + 4] > 0 ? rows[at + 4] : 0);
        cap.maxBitDepth = static_cast<u8>(rows[at + 5] > 0 ? rows[at + 5] : 8);
        cap.concurrentInstances = static_cast<u32>(rows[at + 6] > 0 ? rows[at + 6] : 0);

        if (encoder) {
            if (info.encoderCount >= 8) continue;
            cap.name = "MediaCodec (encoder de hardware)";
            info.encoders[info.encoderCount] = cap;
            info.set_encoder_tag(tag, info.encoderCount);
            ++info.encoderCount;
        } else {
            if (info.decoderCount >= 16) continue;
            cap.name = cap.hardwareAccelerated ? "MediaCodec (hardware)" : "MediaCodec (software)";
            info.decoders[info.decoderCount] = cap;
            info.set_decoder_tag(tag, info.decoderCount);
            ++info.decoderCount;
        }
    }
}

/// Inicializa o motor SEM superfície: instância e dispositivo Vulkan, renderer,
/// pipelines e a thread de render. A superfície chega depois, pelo SurfaceView.
///
/// `probe` e `codecs` são a medição que a UI fez do aparelho (uma vez só, e
/// guardada — ver `DeviceProfile` no Kotlin). Sem eles o motor decide no
/// conservador, o que num aparelho de verdade significa codec de hardware
/// ignorado e orçamento de memória menor que o possível.
AUREA_JNI jboolean AUREA_FN(nativeInitialize)(JNIEnv* env, jclass, jlong handle, jfloat refreshRate,
                                              jstring cacheDir, jstring documentsDir, jboolean debug,
                                              jlongArray probe, jintArray codecs) {
    NativeContext* c = ctx_of(handle);
    if (!c) return JNI_FALSE;
    if (c->initialized) return JNI_TRUE;

    PlatformInfo info;
    bool hasInfo = false;
    if (probe) {
        const jsize n = env->GetArrayLength(probe);
        jlong* p = env->GetLongArrayElements(probe, nullptr);
        if (p) {
            read_probe(p, n, info);
            env->ReleaseLongArrayElements(probe, p, JNI_ABORT);
            hasInfo = true;
        }
    }
    if (codecs) {
        const jsize n = env->GetArrayLength(codecs);
        jint* rows = env->GetIntArrayElements(codecs, nullptr);
        if (rows) {
            read_codecs(rows, n, info);
            env->ReleaseIntArrayElements(codecs, rows, JNI_ABORT);
            hasInfo = true;
        }
    }

    EngineConfig config;
    config.backend = new (std::nothrow) vk::Backend();
    config.backendConfig.enableValidation = debug == JNI_TRUE;
    config.backendConfig.enableGpuTimers = true;
    config.cacheDirectory = to_string(env, cacheDir);
    config.documentsDirectory = to_string(env, documentsDir);
    config.displayRefreshRate = refreshRate > 0.0f ? refreshRate : 60.0f;
    config.platformInfo = info;
    config.hasPlatformInfo = hasInfo;
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

AUREA_JNI jint AUREA_FN(nativeQueryEffectSpecs)(JNIEnv* env, jclass, jlong handle, jint typeId,
                                                jobject rows, jint capacity, jobject blob) {
    NativeContext* c = ctx_of(handle);
    auto* out = static_cast<bridge::EffectParamRow*>(buffer_ptr(env, rows));
    auto* text = static_cast<char*>(buffer_ptr(env, blob));
    if (!c || !out || !text) return 0;
    return static_cast<jint>(c->engine.query_effect_specs(
        static_cast<u32>(typeId), out, row_capacity<bridge::EffectParamRow>(env, rows, capacity), text,
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

/// A prévia de um efeito em RGBA8 sRGB; `outSize` recebe largura/altura.
AUREA_JNI jboolean AUREA_FN(nativeRenderEffectPreview)(JNIEnv* env, jclass, jlong handle, jint typeId,
                                                       jint width, jint height, jobject out,
                                                       jintArray outSize) {
    NativeContext* c = ctx_of(handle);
    auto* dst = static_cast<u8*>(buffer_ptr(env, out));
    if (!c || !dst || width <= 0 || height <= 0) return JNI_FALSE;
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    if (!c->engine.render_effect_preview(static_cast<u32>(typeId), static_cast<u32>(width),
                                         static_cast<u32>(height), rgba, w, h).ok()) {
        return JNI_FALSE;
    }
    if (rgba.empty() || static_cast<jlong>(rgba.size()) > buffer_capacity(env, out)) return JNI_FALSE;
    std::memcpy(dst, rgba.data(), rgba.size());
    if (outSize && env->GetArrayLength(outSize) >= 2) {
        const jint wh[2] = {static_cast<jint>(w), static_cast<jint>(h)};
        env->SetIntArrayRegion(outSize, 0, 2, wh);
    }
    return JNI_TRUE;
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

/// Desagrupar: nulo = feito; senão o motivo da recusa (frase para a UI).
AUREA_JNI jstring AUREA_FN(nativeUngroupPrecomp)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (!c) return env->NewStringUTF("motor indisponivel");
    std::string why;
    const Result<u32> r = c->engine.ungroup_precomp(static_cast<u64>(layer), &why);
    if (r.ok()) return nullptr;
    return env->NewStringUTF(why.empty() ? "nao deu para desagrupar" : why.c_str());
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

// --- Máscaras (roto) e track matte ----------------------------------------------
namespace {
/// Pontos do caminho (6 floats cada) de um FloatArray; nulo = sem pontos.
std::vector<f32> mask_points(JNIEnv* env, jfloatArray pts, jint count, u32& n) {
    std::vector<f32> v;
    n = 0;
    if (!pts || count <= 0) return v;
    const jsize len = env->GetArrayLength(pts);
    n = static_cast<u32>(std::min<jint>(count, len / 6));
    v.resize(static_cast<usize>(n) * 6);
    if (n) env->GetFloatArrayRegion(pts, 0, static_cast<jsize>(v.size()), v.data());
    return v;
}
} // namespace

AUREA_JNI jint AUREA_FN(nativeAddMask)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray pts, jint count, jboolean closed) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -1;
    u32 n = 0;
    const std::vector<f32> v = mask_points(env, pts, count, n);
    return c->engine.add_mask(static_cast<u64>(layer), v.data(), n, closed == JNI_TRUE);
}

AUREA_JNI jboolean AUREA_FN(nativeRemoveMask)(JNIEnv*, jclass, jlong handle, jlong layer, jint mask) {
    NativeContext* c = ctx_of(handle);
    return c && mask >= 0 && c->engine.remove_mask(static_cast<u64>(layer), static_cast<u32>(mask)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetMaskPath)(JNIEnv* env, jclass, jlong handle, jlong layer, jint mask, jfloatArray pts, jint count,
                                               jboolean closed, jboolean undo) {
    NativeContext* c = ctx_of(handle);
    if (!c || mask < 0) return JNI_FALSE;
    u32 n = 0;
    const std::vector<f32> v = mask_points(env, pts, count, n);
    return c->engine.set_mask_path(static_cast<u64>(layer), static_cast<u32>(mask), v.data(), n, closed == JNI_TRUE, undo == JNI_TRUE)
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetMaskProps)(JNIEnv*, jclass, jlong handle, jlong layer, jint mask, jint op, jboolean inverted,
                                                jfloat feather, jfloat expansion, jfloat opacity) {
    NativeContext* c = ctx_of(handle);
    return c && mask >= 0 && op >= 0
               && c->engine.set_mask_props(static_cast<u64>(layer), static_cast<u32>(mask), static_cast<u32>(op), inverted == JNI_TRUE,
                                           feather, expansion, opacity) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeToggleMaskPathKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint mask) {
    NativeContext* c = ctx_of(handle);
    bool keyed = false;
    if (!c || mask < 0 || !c->engine.toggle_mask_path_key(static_cast<u64>(layer), static_cast<u32>(mask), &keyed)) return -1;
    return keyed ? 1 : 0;
}

/// Floats necessários (escreve só se couberem em `out`).
AUREA_JNI jint AUREA_FN(nativeQueryMasks)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c) return 0;
    const jsize cap = out ? env->GetArrayLength(out) : 0;
    std::vector<f32> v(static_cast<usize>(cap));
    const u32 need = c->engine.query_masks(static_cast<u64>(layer), cap ? v.data() : nullptr, static_cast<u32>(cap));
    if (need && need <= static_cast<u32>(cap)) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(need), v.data());
    return static_cast<jint>(need);
}

/// Quadros rastreados, ou −Errc.
AUREA_JNI jint AUREA_FN(nativeTrackMask)(JNIEnv*, jclass, jlong handle, jlong layer, jint mask, jint mode) {
    NativeContext* c = ctx_of(handle);
    if (!c || mask < 0) return -static_cast<jint>(Errc::InvalidState);
    const Result<u32> r = c->engine.track_mask(static_cast<u64>(layer), static_cast<u32>(mask), static_cast<u32>(std::max(0, mode)));
    if (!r.ok()) return -static_cast<jint>(r.status().code());
    return static_cast<jint>(*r);
}

AUREA_JNI jboolean AUREA_FN(nativeSetTrackMatte)(JNIEnv*, jclass, jlong handle, jlong layer, jlong matte, jint mode) {
    NativeContext* c = ctx_of(handle);
    return c && mode >= 0 && c->engine.set_track_matte(static_cast<u64>(layer), static_cast<u64>(matte), static_cast<u32>(mode))
               ? JNI_TRUE : JNI_FALSE;
}

/// {matte, modo} em `out` (matte 0 = nenhuma).
AUREA_JNI jboolean AUREA_FN(nativeQueryTrackMatte)(JNIEnv* env, jclass, jlong handle, jlong layer, jlongArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 2) return JNI_FALSE;
    u64 matte = 0;
    u32 mode = 0;
    if (!c->engine.query_track_matte(static_cast<u64>(layer), matte, mode)) return JNI_FALSE;
    const jlong v[2] = {static_cast<jlong>(matte), static_cast<jlong>(mode)};
    env->SetLongArrayRegion(out, 0, 2, v);
    return JNI_TRUE;
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

AUREA_JNI jboolean AUREA_FN(nativeSetLayerAdjustment)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_layer_adjustment(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetLayerGuide)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_layer_guide(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetLayerLabel)(JNIEnv*, jclass, jlong handle, jlong layer, jint label) {
    NativeContext* c = ctx_of(handle);
    return c && label >= 0 && c->engine.set_layer_label(static_cast<u64>(layer), static_cast<u32>(label)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetLayerSolo)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_layer_solo(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

/// Camadas cujo nome/texto contém a consulta (frente → fundo). Nunca nulo.
AUREA_JNI jlongArray AUREA_FN(nativeSearchLayers)(JNIEnv* env, jclass, jlong handle, jstring query) {
    NativeContext* c = ctx_of(handle);
    std::vector<u64> ids;
    if (c && query) ids = c->engine.search_layers(to_string(env, query));
    jlongArray arr = env->NewLongArray(static_cast<jsize>(ids.size()));
    if (arr && !ids.empty()) {
        std::vector<jlong> v(ids.begin(), ids.end());
        env->SetLongArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    }
    return arr;
}

/// Estado térmico do PowerManager (THERMAL_STATUS_*: 0 nenhum … 6 desligando).
AUREA_JNI void AUREA_FN(nativeSetThermal)(JNIEnv*, jclass, jlong handle, jint status) {
    NativeContext* c = ctx_of(handle);
    if (!c) return;
    // Android: 0 NONE, 1 LIGHT, 2 MODERATE, 3 SEVERE, 4 CRITICAL, 5 EMERGENCY, 6 SHUTDOWN.
    const u32 level = status <= 0 ? 0u : status == 1 ? 1u : status == 2 ? 1u : status == 3 ? 2u : status == 4 ? 3u : 4u;
    c->engine.set_thermal(level, status >= 2);
}

/// O que o motor decidiu para ESTE aparelho, em números.
///
/// A UI mostra o que foi decidido — não um "otimizado!" sem lastro. Cada slot
/// é uma decisão que o motor tomou a partir da sondagem:
///   0 núcleos · 1 núcleos grandes · 2 núcleos pequenos · 3 RAM total (MB)
///   4 RAM disponível (MB) · 5 orçamento do motor (MB) · 6 maior textura
///   7 largura máxima de preview · 8 altura máxima de preview
///   9 largura máxima de export · 10 altura máxima de export
///   11 decodes paralelos · 12 workers do pool · 13 escala inicial (0..3)
/// Foto de base das prévias de efeito: RGBA8 (alfa reto), largura × altura.
AUREA_JNI jboolean AUREA_FN(nativeSetEffectPreviewSource)(JNIEnv* env, jclass, jlong handle, jbyteArray rgba, jint width, jint height) {
    NativeContext* c = ctx_of(handle);
    if (!c || !rgba || width <= 0 || height <= 0) return JNI_FALSE;
    const jsize n = env->GetArrayLength(rgba);
    if (n < width * height * 4) return JNI_FALSE;
    std::vector<u8> px(static_cast<usize>(n));
    env->GetByteArrayRegion(rgba, 0, n, reinterpret_cast<jbyte*>(px.data()));
    return c->engine.set_effect_preview_source(px.data(), static_cast<u32>(width), static_cast<u32>(height)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeDeviceReport)(JNIEnv* env, jclass, jlong handle, jlongArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out) return JNI_FALSE;
    if (env->GetArrayLength(out) < 14) return JNI_FALSE;
    const DeviceCapabilities& caps = c->engine.caps();

    const jlong mb = 1024 * 1024;
    jlong report[14];
    report[0]  = static_cast<jlong>(caps.cpu().totalCores);
    report[1]  = static_cast<jlong>(caps.cpu().performanceCores);
    report[2]  = static_cast<jlong>(caps.cpu().efficiencyCores);
    report[3]  = static_cast<jlong>(caps.cpu().totalMemoryBytes / mb);
    report[4]  = static_cast<jlong>(caps.cpu().availableMemoryBytes / mb);
    report[5]  = static_cast<jlong>(caps.memory_budget_bytes() / mb);
    report[6]  = static_cast<jlong>(caps.max_texture_dimension());
    report[7]  = static_cast<jlong>(caps.max_preview_width());
    report[8]  = static_cast<jlong>(caps.max_preview_height());
    report[9]  = static_cast<jlong>(caps.max_export_width());
    report[10] = static_cast<jlong>(caps.max_export_height());
    report[11] = static_cast<jlong>(caps.decode_parallelism());
    report[12] = static_cast<jlong>(caps.recommended_worker_count());
    report[13] = static_cast<jlong>(caps.recommended_initial_scale(1920, 1080));
    env->SetLongArrayRegion(out, 0, 14, report);
    return JNI_TRUE;
}

/// Nome da GPU, versão do driver e o resumo de uma linha — para a tela de
/// Ajustes mostrar o aparelho, não um rótulo genérico.
AUREA_JNI jstring AUREA_FN(nativeDeviceSummary)(JNIEnv* env, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return env->NewStringUTF("");
    const DeviceCapabilities& caps = c->engine.caps();
    std::string s = caps.gpu().deviceName;
    if (!caps.gpu().driverVersion.empty()) s += " · " + caps.gpu().driverVersion;
    if (s.empty()) s = "GPU não identificada";
    return env->NewStringUTF(s.c_str());
}

namespace {
std::string font_line(const aurea::text::FontEntry& e) {
    return e.family + "\t" + e.style + "\t" + std::to_string(e.weight) + "\t" + (e.italic ? "1" : "0") + "\t" + e.path + "\t" + (e.imported ? "1" : "0");
}
} // namespace

/// Fontes (uma por linha: família, estilo, peso, itálico, caminho, importada).
AUREA_JNI jstring AUREA_FN(nativeListFonts)(JNIEnv* env, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    std::string out;
    for (const auto& e : c->engine.list_fonts()) { out += font_line(e); out += '\n'; }
    return env->NewStringUTF(out.c_str());
}

AUREA_JNI jstring AUREA_FN(nativeImportFont)(JNIEnv* env, jclass, jlong handle, jstring path) {
    NativeContext* c = ctx_of(handle);
    if (!c || !path) return nullptr;
    const char* p = env->GetStringUTFChars(path, nullptr);
    auto r = c->engine.import_font(p);
    env->ReleaseStringUTFChars(path, p);
    return r.ok() ? env->NewStringUTF(font_line(*r).c_str()) : nullptr;
}

AUREA_JNI jboolean AUREA_FN(nativeSetTextFont)(JNIEnv* env, jclass, jlong handle, jlong layer, jstring family, jint weight,
                                              jboolean italic, jstring path) {
    NativeContext* c = ctx_of(handle);
    if (!c || !family || !path) return JNI_FALSE;
    const char* fa = env->GetStringUTFChars(family, nullptr);
    const char* pa = env->GetStringUTFChars(path, nullptr);
    const bool ok = c->engine.set_text_font(static_cast<u64>(layer), fa, static_cast<u32>(weight), italic == JNI_TRUE, pa);
    env->ReleaseStringUTFChars(family, fa);
    env->ReleaseStringUTFChars(path, pa);
    return ok ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetTextStyle)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray in) {
    NativeContext* c = ctx_of(handle);
    if (!c || !in || env->GetArrayLength(in) < 18) return JNI_FALSE;
    f32 v[18];
    env->GetFloatArrayRegion(in, 0, 18, v);
    return c->engine.set_text_style(static_cast<u64>(layer), v) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryTextStyle)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 18) return JNI_FALSE;
    f32 v[18];
    if (!c->engine.query_text_style(static_cast<u64>(layer), v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 18, v);
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetTextSpan)(JNIEnv*, jclass, jlong handle, jlong layer, jint start, jint end, jboolean hasColor,
                                              jfloat r, jfloat g, jfloat b, jint weight, jfloat scale) {
    NativeContext* c = ctx_of(handle);
    return c && start >= 0 && end > start
                   && c->engine.set_text_span(static_cast<u64>(layer), static_cast<u32>(start), static_cast<u32>(end), hasColor == JNI_TRUE,
                                              Vec4{r, g, b, 1.0f}, static_cast<u32>(std::max(0, weight)), scale)
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeClearTextSpans)(JNIEnv*, jclass, jlong handle, jlong layer, jint start, jint end) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.clear_text_spans(static_cast<u64>(layer), static_cast<u32>(std::max(0, start)), static_cast<u32>(std::max(0, end)))
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jstring AUREA_FN(nativeLayerMediaPath)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    const std::string p = c->engine.layer_media_path(static_cast<u64>(layer));
    return p.empty() ? nullptr : env->NewStringUTF(p.c_str());
}

/// Legendas: palavras (texto + [início, fim] em segundos da mídia) e opções.
/// ints = modo, palavras, caracteres, linhas, estilo, destaque, maiúsculas,
/// quebrar nas pausas, tirar vícios; floats = pausa, y, tamanho, cor (rgb).
AUREA_JNI jint AUREA_FN(nativeCreateCaptions)(JNIEnv* env, jclass, jlong handle, jlong layer, jobjectArray texts, jdoubleArray times,
                                             jintArray ints, jfloatArray floats) {
    NativeContext* c = ctx_of(handle);
    if (!c || !texts || !times || !ints || !floats) return -1;
    const jsize n = env->GetArrayLength(texts);
    if (env->GetArrayLength(times) < n * 2 || env->GetArrayLength(ints) < 9 || env->GetArrayLength(floats) < 6) return -1;
    std::vector<f64> t(static_cast<usize>(n) * 2);
    env->GetDoubleArrayRegion(times, 0, n * 2, t.data());
    std::vector<text::CaptionWord> words(static_cast<usize>(n));
    for (jsize i = 0; i < n; ++i) {
        auto js = static_cast<jstring>(env->GetObjectArrayElement(texts, i));
        const char* s = js ? env->GetStringUTFChars(js, nullptr) : nullptr;
        words[static_cast<usize>(i)] = text::CaptionWord{s ? s : "", t[static_cast<usize>(i) * 2], t[static_cast<usize>(i) * 2 + 1]};
        if (s) env->ReleaseStringUTFChars(js, s);
        if (js) env->DeleteLocalRef(js);
    }
    jint iv[9];
    jfloat fv[6];
    env->GetIntArrayRegion(ints, 0, 9, iv);
    env->GetFloatArrayRegion(floats, 0, 6, fv);
    text::CaptionOptions o;
    o.mode = static_cast<u32>(std::max(0, iv[0]));
    o.maxWords = static_cast<u32>(std::max(1, iv[1]));
    o.maxChars = static_cast<u32>(std::max(4, iv[2]));
    o.maxLines = static_cast<u32>(std::max(1, iv[3]));
    o.style = static_cast<u32>(std::max(0, iv[4]));
    o.highlight = iv[5] != 0;
    o.uppercase = iv[6] != 0;
    o.breakOnPause = iv[7] != 0;
    o.pauseSec = fv[0];
    o.posY = fv[1];
    o.sizeFrac = fv[2];
    o.highlightColor = Vec4{fv[3], fv[4], fv[5], 1.0f};
    if (iv[8] != 0) words = text::remove_filler_words(words);
    auto r = c->engine.create_captions(static_cast<u64>(layer), words, o);
    return r.ok() ? static_cast<jint>(*r) : -static_cast<jint>(r.status().code());
}

AUREA_JNI jint AUREA_FN(nativeRemoveCaptions)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.remove_captions(static_cast<u64>(layer))) : 0;
}

AUREA_JNI jint AUREA_FN(nativeCaptionCount)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.caption_count(static_cast<u64>(layer))) : 0;
}

/// SRT → "início\tfim\tpalavra" por linha.
AUREA_JNI jstring AUREA_FN(nativeParseSrt)(JNIEnv* env, jclass, jstring srt) {
    if (!srt) return nullptr;
    const char* s = env->GetStringUTFChars(srt, nullptr);
    const std::vector<text::CaptionWord> w = text::parse_srt(s ? s : "");
    if (s) env->ReleaseStringUTFChars(srt, s);
    std::string out;
    char buf[64];
    for (const text::CaptionWord& x : w) {
        std::snprintf(buf, sizeof buf, "%.3f\t%.3f\t", x.start, x.end);
        out += buf;
        out += x.text;
        out += '\n';
    }
    return env->NewStringUTF(out.c_str());
}

/// Vícios de linguagem: o mesmo critério do motor, para a transcrição marcar.
AUREA_JNI jboolean AUREA_FN(nativeIsFillerWord)(JNIEnv* env, jclass, jstring word) {
    if (!word) return JNI_FALSE;
    const char* s = env->GetStringUTFChars(word, nullptr);
    const bool f = s && text::is_filler_word(s);
    if (s) env->ReleaseStringUTFChars(word, s);
    return f ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jfloatArray AUREA_FN(nativeQueryTextAnimators)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    std::vector<f32> v(64 * Engine::kTextAnimFloats);
    const u32 n = c->engine.query_text_animators(static_cast<u64>(layer), v.data(), static_cast<u32>(v.size()));
    jfloatArray out = env->NewFloatArray(static_cast<jsize>(n * Engine::kTextAnimFloats));
    if (out && n) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(n * Engine::kTextAnimFloats), v.data());
    return out;
}

AUREA_JNI jint AUREA_FN(nativeAddTextAnimator)(JNIEnv*, jclass, jlong handle, jlong layer, jint props) {
    NativeContext* c = ctx_of(handle);
    return c ? c->engine.add_text_animator(static_cast<u64>(layer), static_cast<u32>(props)) : -1;
}

AUREA_JNI jboolean AUREA_FN(nativeRemoveTextAnimator)(JNIEnv*, jclass, jlong handle, jlong layer, jint index) {
    NativeContext* c = ctx_of(handle);
    return c && index >= 0 && c->engine.remove_text_animator(static_cast<u64>(layer), static_cast<u32>(index)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetTextAnimator)(JNIEnv* env, jclass, jlong handle, jlong layer, jint index, jfloatArray in) {
    NativeContext* c = ctx_of(handle);
    if (!c || !in || index < 0 || env->GetArrayLength(in) < static_cast<jsize>(Engine::kTextAnimFloats)) return JNI_FALSE;
    f32 v[Engine::kTextAnimFloats];
    env->GetFloatArrayRegion(in, 0, Engine::kTextAnimFloats, v);
    return c->engine.set_text_animator(static_cast<u64>(layer), static_cast<u32>(index), v) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetTextAnimParam)(JNIEnv*, jclass, jlong handle, jlong layer, jint index, jint param, jfloat value) {
    NativeContext* c = ctx_of(handle);
    return c && index >= 0 && param >= 0
                   && c->engine.set_text_anim_param(static_cast<u64>(layer), static_cast<u32>(index), static_cast<u32>(param), value)
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeToggleTextAnimKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint index, jint param) {
    NativeContext* c = ctx_of(handle);
    return c && index >= 0 && param >= 0
                   && c->engine.toggle_text_anim_key(static_cast<u64>(layer), static_cast<u32>(index), static_cast<u32>(param))
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeApplyTextPreset)(JNIEnv*, jclass, jlong handle, jlong layer, jint preset) {
    NativeContext* c = ctx_of(handle);
    return c && preset >= 0 && c->engine.apply_text_preset(static_cast<u64>(layer), static_cast<u32>(preset)) ? JNI_TRUE : JNI_FALSE;
}

// =============================================================================
// Presets (JSON em project/Presets.hpp). O texto cruza a fronteira como bytes
// UTF-8 — nada de "modified UTF-8" do NewStringUTF num nome com emoji.
// =============================================================================
namespace {
std::string utf8_of(JNIEnv* env, jbyteArray a) {
    if (!a) return {};
    const jsize n = env->GetArrayLength(a);
    std::string s(static_cast<usize>(n), '\0');
    if (n > 0) env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(s.data()));
    return s;
}
jbyteArray bytes_of(JNIEnv* env, const std::string& s) {
    jbyteArray out = env->NewByteArray(static_cast<jsize>(s.size()));
    if (out && !s.empty()) env->SetByteArrayRegion(out, 0, static_cast<jsize>(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    return out;
}
} // namespace

/// Preset da camada: kind 0 efeitos, 1 texto, 2 animação; parts = TextPresetParts.
/// Nulo = a camada não tem o que salvar desse tipo.
AUREA_JNI jbyteArray AUREA_FN(nativeSavePreset)(JNIEnv* env, jclass, jlong handle, jlong layer, jint kind, jbyteArray name, jint parts) {
    NativeContext* c = ctx_of(handle);
    if (!c || kind < 0 || kind > static_cast<jint>(presets::PresetKind::Animation)) return nullptr;
    const std::string js = c->engine.save_preset(static_cast<u64>(layer), static_cast<presets::PresetKind>(kind), utf8_of(env, name),
                                                 static_cast<u32>(parts));
    return js.empty() ? nullptr : bytes_of(env, js);
}

/// Aplica (um passo de desfazer). Nulo = aplicado; senão, o motivo (UTF-8).
AUREA_JNI jbyteArray AUREA_FN(nativeApplyPreset)(JNIEnv* env, jclass, jlong handle, jlong layer, jbyteArray json, jlong duration) {
    NativeContext* c = ctx_of(handle);
    if (!c) return bytes_of(env, "motor indisponivel");
    std::string err;
    if (c->engine.apply_preset(static_cast<u64>(layer), utf8_of(env, json), static_cast<i64>(duration), &err)) return nullptr;
    return bytes_of(env, err.empty() ? std::string("preset nao aplicado") : err);
}

/// Preset de legenda: ints/floats no MESMO layout do nativeCreateCaptions.
AUREA_JNI jbyteArray AUREA_FN(nativeMakeCaptionPreset)(JNIEnv* env, jclass, jbyteArray name, jintArray ints, jfloatArray floats) {
    if (!ints || !floats || env->GetArrayLength(ints) < 9 || env->GetArrayLength(floats) < 6) return nullptr;
    jint iv[9];
    jfloat fv[6];
    env->GetIntArrayRegion(ints, 0, 9, iv);
    env->GetFloatArrayRegion(floats, 0, 6, fv);
    text::CaptionOptions o;
    o.mode = static_cast<u32>(std::clamp(iv[0], 0, 1));
    o.maxWords = static_cast<u32>(std::clamp(iv[1], 1, 20));
    o.maxChars = static_cast<u32>(std::clamp(iv[2], 4, 80));
    o.maxLines = static_cast<u32>(std::clamp(iv[3], 1, 5));
    o.style = static_cast<u32>(std::clamp(iv[4], 0, static_cast<jint>(text::kCaptionStyleCount) - 1));
    o.highlight = iv[5] != 0;
    o.uppercase = iv[6] != 0;
    o.breakOnPause = iv[7] != 0;
    o.pauseSec = std::clamp(fv[0], 0.05f, 5.0f);
    o.posY = std::clamp(fv[1], 0.1f, 0.9f);
    o.sizeFrac = std::clamp(fv[2], 0.01f, 0.3f);
    o.highlightColor = Vec4{std::clamp(fv[3], 0.0f, 1.0f), std::clamp(fv[4], 0.0f, 1.0f), std::clamp(fv[5], 0.0f, 1.0f), 1.0f};
    return bytes_of(env, presets::make_caption_preset(utf8_of(env, name), o, iv[8] != 0));
}

/// Lê um preset de legenda: [modo, palavras, caracteres, linhas, estilo,
/// destaque, maiúsculas, pausas, vícios, pausa s, y, tamanho, r, g, b]. Nulo = inválido.
AUREA_JNI jfloatArray AUREA_FN(nativeParseCaptionPreset)(JNIEnv* env, jclass, jbyteArray json) {
    presets::Preset p;
    if (!presets::parse(utf8_of(env, json), p) || p.kind != presets::PresetKind::Caption) return nullptr;
    const text::CaptionOptions& o = p.caption;
    const f32 v[15] = {static_cast<f32>(o.mode), static_cast<f32>(o.maxWords), static_cast<f32>(o.maxChars), static_cast<f32>(o.maxLines),
                       static_cast<f32>(o.style), o.highlight ? 1.0f : 0.0f, o.uppercase ? 1.0f : 0.0f, o.breakOnPause ? 1.0f : 0.0f,
                       p.removeFillers ? 1.0f : 0.0f, o.pauseSec, o.posY, o.sizeFrac, o.highlightColor.x, o.highlightColor.y, o.highlightColor.z};
    jfloatArray out = env->NewFloatArray(15);
    if (out) env->SetFloatArrayRegion(out, 0, 15, v);
    return out;
}

AUREA_JNI jbyteArray AUREA_FN(nativeMakeCurvePreset)(JNIEnv* env, jclass, jbyteArray name, jint interp, jfloat x1, jfloat y1, jfloat x2, jfloat y2) {
    const jint i = std::clamp(interp, 0, static_cast<jint>(Interpolation::CustomCurve));
    return bytes_of(env, presets::make_curve_preset(utf8_of(env, name), static_cast<Interpolation>(i), std::clamp(x1, 0.0f, 1.0f),
                                                    std::clamp(y1, -2.0f, 3.0f), std::clamp(x2, 0.0f, 1.0f), std::clamp(y2, -2.0f, 3.0f)));
}

/// Lê um preset de curva: [interp, x1, y1, x2, y2]. Nulo = inválido.
AUREA_JNI jfloatArray AUREA_FN(nativeParseCurvePreset)(JNIEnv* env, jclass, jbyteArray json) {
    presets::Preset p;
    if (!presets::parse(utf8_of(env, json), p) || p.kind != presets::PresetKind::Curve) return nullptr;
    const f32 v[5] = {static_cast<f32>(p.curveInterp), p.x1, p.y1, p.x2, p.y2};
    jfloatArray out = env->NewFloatArray(5);
    if (out) env->SetFloatArrayRegion(out, 0, 5, v);
    return out;
}

// =============================================================================
// Expressões. Texto atravessa em BYTES UTF-8 (não jstring): o JNI fala "UTF-8
// modificado", e um emoji no comentário da expressão (4 bytes) viraria abort
// no CheckJNI. Campos separados por \x1F; a fonte, quando vem, é o ÚLTIMO campo.
// =============================================================================
namespace {
std::string bytes_to_string(JNIEnv* env, jbyteArray a) {
    if (!a) return {};
    const jsize n = env->GetArrayLength(a);
    std::string s(static_cast<usize>(n), '\0');
    if (n) env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(s.data()));
    return s;
}
jbyteArray string_to_bytes(JNIEnv* env, const std::string& s) {
    jbyteArray out = env->NewByteArray(static_cast<jsize>(s.size()));
    if (out && !s.empty()) env->SetByteArrayRegion(out, 0, static_cast<jsize>(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    return out;
}
std::string diag_fields(const expr::Diagnostic& d) {
    return std::string(d.ok ? "1" : "0") + '\x1F' + std::to_string(d.line) + '\x1F' + std::to_string(d.column) + '\x1F' + d.message;
}
} // namespace

/// Trincas (property, effectIndex, paramIndex) de um IntArray; vazio se inválido.
static std::vector<u32> read_keys(JNIEnv* env, jintArray keys) {
    std::vector<u32> k;
    if (!keys) return k;
    const jsize n = env->GetArrayLength(keys);
    if (n <= 0 || n % 3 != 0 || n > 48) return k;
    k.resize(static_cast<usize>(n));
    env->GetIntArrayRegion(keys, 0, n, reinterpret_cast<jint*>(k.data()));
    return k;
}

/// Grava o MESMO texto nas trilhas `keys` (trincas), num passo de desfazer;
/// vazio remove. Devolve "ok US linha US coluna US mensagem" da sintaxe; nulo =
/// camada/propriedade inválida.
AUREA_JNI jbyteArray AUREA_FN(nativeSetExpression)(JNIEnv* env, jclass, jlong handle, jlong layer, jintArray keys, jbyteArray source) {
    NativeContext* c = ctx_of(handle);
    const std::vector<u32> k = read_keys(env, keys);
    if (!c || k.empty()) return nullptr;
    const std::string src = bytes_to_string(env, source);
    expr::Diagnostic d;
    const Status s = c->engine.set_expressions(static_cast<u64>(layer), k.data(), static_cast<u32>(k.size() / 3), src.c_str(), &d);
    if (!s.ok()) return nullptr;
    return string_to_bytes(env, diag_fields(d));
}

AUREA_JNI jboolean AUREA_FN(nativeSetExpressionEnabled)(JNIEnv* env, jclass, jlong handle, jlong layer, jintArray keys, jboolean enabled) {
    NativeContext* c = ctx_of(handle);
    const std::vector<u32> k = read_keys(env, keys);
    return c && !k.empty()
                   && c->engine.set_expressions_enabled(static_cast<u64>(layer), k.data(), static_cast<u32>(k.size() / 3), enabled == JNI_TRUE)
               ? JNI_TRUE : JNI_FALSE;
}

/// "existe US ligada US ok US linha US coluna US mensagem US valor US fonte"
/// (valor no playhead, unidade guardada). Nulo = camada não existe.
AUREA_JNI jbyteArray AUREA_FN(nativeQueryExpression)(JNIEnv* env, jclass, jlong handle, jlong layer, jint property,
                                                      jint effectIndex, jint paramIndex) {
    NativeContext* c = ctx_of(handle);
    if (!c || property < 0) return nullptr;
    Engine::ExpressionInfo info;
    if (!c->engine.query_expression(static_cast<u64>(layer), static_cast<u32>(property), static_cast<u32>(effectIndex),
                                    static_cast<u32>(paramIndex), info)) {
        return nullptr;
    }
    char value[32];
    std::snprintf(value, sizeof(value), "%.6g", static_cast<double>(info.value));
    const std::string out = std::string(info.exists ? "1" : "0") + '\x1F' + (info.enabled ? "1" : "0") + '\x1F'
                          + diag_fields(info.error) + '\x1F' + value + '\x1F' + info.source;
    return string_to_bytes(env, out);
}

/// Trilhas com expressão na camada: 4 ints por linha (property, effectIndex,
/// paramIndex, flags 1 ligada | 2 com erro).
AUREA_JNI jintArray AUREA_FN(nativeQueryExpressions)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    std::vector<u32> rows(4 * (kMaxTrackCount + 1));
    const u32 n = c->engine.query_expressions(static_cast<u64>(layer), rows.data(), kMaxTrackCount + 1);
    jintArray out = env->NewIntArray(static_cast<jsize>(n * 4));
    if (out && n) env->SetIntArrayRegion(out, 0, static_cast<jsize>(n * 4), reinterpret_cast<const jint*>(rows.data()));
    return out;
}

/// Só a sintaxe (a folha valida enquanto a pessoa digita). Sem motor.
AUREA_JNI jbyteArray AUREA_FN(nativeCheckExpressionSyntax)(JNIEnv* env, jclass, jbyteArray source) {
    return string_to_bytes(env, diag_fields(expr::check_syntax(bytes_to_string(env, source))));
}

/// Fonte da camada de texto: "família\tpeso\titálico\tcaminho" (nulo = não é texto).
AUREA_JNI jstring AUREA_FN(nativeTextFont)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    const std::string s = c->engine.text_font(static_cast<u64>(layer));
    return s.empty() ? nullptr : env->NewStringUTF(s.c_str());
}

AUREA_JNI jboolean AUREA_FN(nativeSetVectorBlur)(JNIEnv*, jclass, jlong handle, jlong layer, jfloat amount) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_vector_blur(static_cast<u64>(layer), amount) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeStartCameraTrack)(JNIEnv*, jclass, jlong handle, jlong layer, jint mode) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.start_camera_track(static_cast<u64>(layer), static_cast<u32>(mode)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI void AUREA_FN(nativeCancelCameraTrack)(JNIEnv*, jclass, jlong handle) {
    if (NativeContext* c = ctx_of(handle)) c->engine.cancel_camera_track();
}

/// {estado, progresso, quadros, resolvidos, rastros, pontos, erro px, confiança,
///  FOV°, só rotação, do cache}; devolve a mensagem.
AUREA_JNI jstring AUREA_FN(nativeCameraTrackStatus)(JNIEnv* env, jclass, jlong handle, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 11) return nullptr;
    const Engine::CameraTrackStatus s = c->engine.camera_track_status();
    const f32 v[11] = {static_cast<f32>(s.state), s.progress, static_cast<f32>(s.frames), static_cast<f32>(s.framesSolved),
                       static_cast<f32>(s.tracks), static_cast<f32>(s.inliers), s.rmsError, s.confidence, s.fovDeg,
                       s.rotationOnly ? 1.0f : 0.0f, s.cached ? 1.0f : 0.0f};
    env->SetFloatArrayRegion(out, 0, 11, v);
    return env->NewStringUTF(s.message.c_str());
}

/// Pontos seguidos no quadro (x, y, estado) em px da composição; devolve quantos.
AUREA_JNI jint AUREA_FN(nativeCameraTrackFeatures)(JNIEnv* env, jclass, jlong handle, jlong frame, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out) return 0;
    const jsize cap = env->GetArrayLength(out) / 3;
    std::vector<f32> v(static_cast<usize>(cap) * 3);
    const u32 n = c->engine.camera_track_features(frame, v.data(), static_cast<u32>(cap));
    if (n) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(n * 3), v.data());
    return static_cast<jint>(n);
}

AUREA_JNI jlong AUREA_FN(nativeApplyCameraTrack)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> r = c->engine.apply_camera_track();
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jint AUREA_FN(nativeQueryTimeRemap)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out) return 0;
    const jsize n = env->GetArrayLength(out);
    std::vector<f32> v(static_cast<usize>(n));
    const u32 w = c->engine.query_time_remap(static_cast<u64>(layer), v.data(), static_cast<u32>(n));
    if (w) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(w), v.data());
    return static_cast<jint>(w);
}

AUREA_JNI jint AUREA_FN(nativeEditTimeRemapKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint index, jlong frame,
                                               jfloat value, jint interp) {
    NativeContext* c = ctx_of(handle);
    return c ? c->engine.edit_time_remap_key(static_cast<u64>(layer), index, frame, value, interp) : -1;
}

AUREA_JNI jboolean AUREA_FN(nativeRemoveTimeRemapKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint index) {
    NativeContext* c = ctx_of(handle);
    return c && index >= 0 && c->engine.remove_time_remap_key(static_cast<u64>(layer), static_cast<u32>(index)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetFrameBlend)(JNIEnv*, jclass, jlong handle, jlong layer, jint mode) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_frame_blend(static_cast<u64>(layer), static_cast<u32>(mode)) ? JNI_TRUE : JNI_FALSE;
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

// =============================================================================
// Camada vetorial (Fase 7D). Documento e caminhos como float[] no codec de
// vector/VectorDocument.cpp; nomes dos grupos numa string (um por linha).
// =============================================================================
namespace {
jfloatArray to_float_array(JNIEnv* env, const std::vector<f32>& v) {
    jfloatArray out = env->NewFloatArray(static_cast<jsize>(v.size()));
    if (out && !v.empty()) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
    return out;
}
std::vector<f32> from_float_array(JNIEnv* env, jfloatArray a) {
    std::vector<f32> v;
    if (!a) return v;
    v.resize(static_cast<usize>(env->GetArrayLength(a)));
    if (!v.empty()) env->GetFloatArrayRegion(a, 0, static_cast<jsize>(v.size()), v.data());
    return v;
}
jlong result_id(const Result<u64>& r) { return r.ok() ? static_cast<jlong>(*r) : -static_cast<jlong>(r.status().code()); }
} // namespace

AUREA_JNI jlong AUREA_FN(nativeAddVectorLayer)(JNIEnv*, jclass, jlong handle, jint preset) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    return result_id(c->engine.add_vector_layer(static_cast<u32>(std::max(0, preset))));
}

AUREA_JNI jfloatArray AUREA_FN(nativeVectorDocument)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    std::vector<f32> v;
    std::string names;
    if (!c || !c->engine.vector_document(static_cast<u64>(layer), v, names)) return nullptr;
    return to_float_array(env, v);
}

AUREA_JNI jstring AUREA_FN(nativeVectorGroupNames)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    std::vector<f32> v;
    std::string names;
    if (!c || !c->engine.vector_document(static_cast<u64>(layer), v, names)) return nullptr;
    return env->NewStringUTF(names.c_str());
}

AUREA_JNI jboolean AUREA_FN(nativeSetVectorDocument)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray doc, jstring names, jboolean continuing) {
    NativeContext* c = ctx_of(handle);
    if (!c || !doc) return JNI_FALSE;
    const std::vector<f32> v = from_float_array(env, doc);
    return c->engine.set_vector_document(static_cast<u64>(layer), v.data(), v.size(), to_string(env, names), continuing == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jfloatArray AUREA_FN(nativeVectorPathAt)(JNIEnv* env, jclass, jlong handle, jlong layer, jint group, jint path) {
    NativeContext* c = ctx_of(handle);
    std::vector<f32> v;
    if (!c || group < 0 || path < 0 || !c->engine.vector_path_at(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(path), v)) return nullptr;
    return to_float_array(env, v);
}

AUREA_JNI jboolean AUREA_FN(nativeSetVectorPath)(JNIEnv* env, jclass, jlong handle, jlong layer, jint group, jint path, jfloatArray bez, jboolean continuing) {
    NativeContext* c = ctx_of(handle);
    if (!c || group < 0 || path < 0 || !bez) return JNI_FALSE;
    const std::vector<f32> v = from_float_array(env, bez);
    return c->engine.set_vector_path(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(path), v.data(), v.size(), continuing == JNI_TRUE)
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeToggleVectorPathKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint group, jint path) {
    NativeContext* c = ctx_of(handle);
    return c && group >= 0 && path >= 0 && c->engine.toggle_vector_path_key(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(path))
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeAddVectorGroup)(JNIEnv*, jclass, jlong handle, jlong layer, jint kind) {
    NativeContext* c = ctx_of(handle);
    return c && kind >= 0 ? c->engine.add_vector_group(static_cast<u64>(layer), static_cast<u32>(kind)) : -1;
}

AUREA_JNI jboolean AUREA_FN(nativeRemoveVectorGroup)(JNIEnv*, jclass, jlong handle, jlong layer, jint group) {
    NativeContext* c = ctx_of(handle);
    return c && group >= 0 && c->engine.remove_vector_group(static_cast<u64>(layer), static_cast<u32>(group)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeAddVectorPath)(JNIEnv* env, jclass, jlong handle, jlong layer, jint group, jint kind, jfloatArray bez) {
    NativeContext* c = ctx_of(handle);
    if (!c || group < 0 || kind < 0) return -1;
    const std::vector<f32> v = from_float_array(env, bez);
    return c->engine.add_vector_path(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(kind), v.empty() ? nullptr : v.data(), v.size());
}

AUREA_JNI jboolean AUREA_FN(nativeRemoveVectorPath)(JNIEnv*, jclass, jlong handle, jlong layer, jint group, jint path) {
    NativeContext* c = ctx_of(handle);
    return c && group >= 0 && path >= 0 && c->engine.remove_vector_path(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(path))
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeMakeVectorPathEditable)(JNIEnv*, jclass, jlong handle, jlong layer, jint group, jint path) {
    NativeContext* c = ctx_of(handle);
    return c && group >= 0 && path >= 0 && c->engine.make_vector_path_editable(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(path))
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jfloatArray AUREA_FN(nativeQueryVectorParams)(JNIEnv* env, jclass, jlong handle, jlong layer, jint group) {
    NativeContext* c = ctx_of(handle);
    if (!c || group < 0) return nullptr;
    std::vector<f32> v(Engine::kVectorParamFloats);
    if (c->engine.query_vector_params(static_cast<u64>(layer), static_cast<u32>(group), v.data(), Engine::kVectorParamFloats) == 0) return nullptr;
    return to_float_array(env, v);
}

AUREA_JNI jboolean AUREA_FN(nativeSetVectorParam)(JNIEnv*, jclass, jlong handle, jlong layer, jint group, jint param, jfloat value, jboolean continuing) {
    NativeContext* c = ctx_of(handle);
    return c && group >= 0 && param >= 0
                   && c->engine.set_vector_param(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(param), value, continuing == JNI_TRUE)
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeToggleVectorParamKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint group, jint param) {
    NativeContext* c = ctx_of(handle);
    return c && group >= 0 && param >= 0 && c->engine.toggle_vector_param_key(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(param))
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jfloatArray AUREA_FN(nativeQueryShapeParams)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    std::vector<f32> v(Engine::kShapeParamFloats);
    if (c->engine.query_shape_params(static_cast<u64>(layer), v.data(), Engine::kShapeParamFloats) == 0) return nullptr;
    return to_float_array(env, v);
}

AUREA_JNI jboolean AUREA_FN(nativeSetShapeParamAnim)(JNIEnv*, jclass, jlong handle, jlong layer, jint param, jfloat value, jboolean continuing) {
    NativeContext* c = ctx_of(handle);
    return c && param >= 0 && c->engine.set_shape_param(static_cast<u64>(layer), static_cast<u32>(param), value, continuing == JNI_TRUE)
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeToggleShapeParamKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint param) {
    NativeContext* c = ctx_of(handle);
    return c && param >= 0 && c->engine.toggle_shape_param_key(static_cast<u64>(layer), static_cast<u32>(param)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jlong AUREA_FN(nativeAddFreehandPath)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray xy, jfloat error) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const std::vector<f32> v = from_float_array(env, xy);
    return result_id(c->engine.add_freehand_path(static_cast<u64>(layer), v.data(), v.size(), error));
}

AUREA_JNI jlong AUREA_FN(nativeImportSvg)(JNIEnv* env, jclass, jlong handle, jbyteArray bytes, jstring name) {
    NativeContext* c = ctx_of(handle);
    if (!c || !bytes) return -static_cast<jlong>(Errc::InvalidArgument);
    std::string text(static_cast<usize>(env->GetArrayLength(bytes)), '\0');
    if (!text.empty()) env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(text.size()), reinterpret_cast<jbyte*>(text.data()));
    const std::string n = to_string(env, name);
    return result_id(c->engine.import_svg(text, n.c_str()));
}

AUREA_JNI jboolean AUREA_FN(nativeSetTextPath)(JNIEnv*, jclass, jlong handle, jlong layer, jlong pathLayer, jfloat offset, jboolean perpendicular, jboolean reverse) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_text_path(static_cast<u64>(layer), static_cast<u64>(pathLayer), offset, perpendicular == JNI_TRUE, reverse == JNI_TRUE)
               ? JNI_TRUE : JNI_FALSE;
}

/// Texto no caminho: [camada-guia, margem (bits do float), perpendicular, invertido].
AUREA_JNI jlongArray AUREA_FN(nativeQueryTextPath)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    u64 pl = 0;
    f32 off = 0.0f;
    bool perp = true, rev = false;
    if (!c || !c->engine.query_text_path(static_cast<u64>(layer), pl, off, perp, rev)) return nullptr;
    u32 offBits = 0;
    std::memcpy(&offBits, &off, 4);
    const jlong v[4] = {static_cast<jlong>(pl), static_cast<jlong>(offBits), perp ? 1 : 0, rev ? 1 : 0};
    jlongArray out = env->NewLongArray(4);
    if (out) env->SetLongArrayRegion(out, 0, 4, v);
    return out;
}
