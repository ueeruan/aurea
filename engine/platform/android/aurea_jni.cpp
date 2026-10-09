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
#if defined(AUREA_GPU_GLES)
#include "GlesBackend.hpp"
#endif

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Version.hpp"
#include "aurea/core/GestureMath.hpp"
#include "aurea/core/Trackball.hpp"
#include "aurea/export/BitratePolicy.hpp"
#include "aurea/export/ExportWatchdog.hpp"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <cstdlib>
#include "aurea/platform/AndroidVideoCompatibility.hpp"

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
            // Exceção Java atravessando a fronteira: limpa (senão a ART aborta
            // na próxima chamada JNI) e registra — nunca some em silêncio.
            env->ExceptionClear();
            AUREA_LOG_WARN("openContentFd: excecao Java (midia vira placeholder)");
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
            // Tipicamente OutOfMemoryError do ByteArray da imagem grande.
            env->ExceptionClear();
            AUREA_LOG_WARN("decodeImage: excecao Java (imagem vira placeholder)");
            arr = nullptr;
        }
        if (arr) {
            const jsize n = env->GetArrayLength(arr);
            if (n > 8) {
                u8 header[8];
                env->GetByteArrayRegion(arr, 0, 8, reinterpret_cast<jbyte*>(header));
                const u32 w = header[0] | (header[1] << 8) | (header[2] << 16) | (static_cast<u32>(header[3]) << 24);
                const u32 h = header[4] | (header[5] << 8) | (header[6] << 16) | (static_cast<u32>(header[7]) << 24);
                // Em u64: no armeabi-v7a o usize é de 32 bits e w*h*4 daria a volta.
                const u64 bytes64 = static_cast<u64>(w) * h * 4;
                const usize bytes = static_cast<usize>(bytes64);
                if (w && h && bytes64 + 8 == static_cast<u64>(n)) {
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
    android::MediaCodecFactory media;
    Engine engine;                    ///< morre antes da fábrica usada pelos workers
    std::string startupError;
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

#include "aurea/timeline/CanvasFit.hpp"
#define AUREA_JNI extern "C" JNIEXPORT
#define AUREA_FN(name) JNICALL Java_com_aurea_aurea_engine_AureaEngine_##name

AUREA_JNI jfloat AUREA_FN(clampPinchFactor)(JNIEnv*, jclass, jfloat factor, jfloat x, jfloat y, jfloat z, jboolean threeD) {
    return aurea::clamp_pinch_factor(factor, x, y, z, threeD == JNI_TRUE);
}

AUREA_JNI jfloatArray AUREA_FN(fitCanvas)(JNIEnv* env,jclass,jfloatArray values,jboolean fill) {
    if(!values||env->GetArrayLength(values)!=11)return env->NewFloatArray(0);
    std::array<float,11> a{};env->GetFloatArrayRegion(values,0,11,a.data());
    const auto fit=aurea::canvas_fit(a,fill==JNI_TRUE);
    auto result=env->NewFloatArray(5);if(result)env->SetFloatArrayRegion(result,0,5,fit.data());return result;
}

// Escala 3D de gesto no formato gravado (GestureMath.hpp): o volume nunca estica.
AUREA_JNI jfloatArray AUREA_FN(gestureScale3D)(JNIEnv* env, jclass, jint kind, jfloat x, jfloat y, jfloat z, jint axis, jfloat factor) {
    const bool follows = aurea::scale_z_follows_x(static_cast<aurea::LayerKind>(kind));
    const aurea::Vec3 s = aurea::gesture_scale_3d(aurea::Vec3{x, y, z}, axis, factor, follows);
    const jfloat out[3]{s.x, s.y, s.z};
    jfloatArray arr = env->NewFloatArray(3);
    if (arr) env->SetFloatArrayRegion(arr, 0, 3, out);
    return arr;
}

// Trackball do gizmo de girar (core/Trackball.hpp): um passo do arrasto e a parte sob o dedo.
AUREA_JNI jfloatArray AUREA_FN(trackballDrag)(JNIEnv* env, jclass, jfloatArray args) {
    if (!args || env->GetArrayLength(args) != static_cast<jsize>(aurea::trackball::kDragArgs)) return env->NewFloatArray(0);
    float in[aurea::trackball::kDragArgs]{};
    env->GetFloatArrayRegion(args, 0, aurea::trackball::kDragArgs, in);
    float out[aurea::trackball::kDragOut]{};
    if (!aurea::trackball::drag(in, out)) return env->NewFloatArray(0);
    jfloatArray arr = env->NewFloatArray(aurea::trackball::kDragOut);
    if (arr) env->SetFloatArrayRegion(arr, 0, aurea::trackball::kDragOut, out);
    return arr;
}

AUREA_JNI jint AUREA_FN(trackballHit)(JNIEnv* env, jclass, jfloatArray axes, jfloat x, jfloat y, jfloat radius, jfloat tolerance) {
    if (!axes || env->GetArrayLength(axes) < 9) return aurea::trackball::kNone;
    float a[9]{};
    env->GetFloatArrayRegion(axes, 0, 9, a);
    return aurea::trackball::hit_test(a, x, y, radius, tolerance);
}

AUREA_JNI jfloat AUREA_FN(clampPinchFactor3D)(JNIEnv*, jclass, jint kind, jfloat factor, jfloat x, jfloat y, jfloat z) {
    return aurea::clamp_pinch_factor_3d(factor, aurea::Vec3{x, y, z}, aurea::scale_z_follows_x(static_cast<aurea::LayerKind>(kind)));
}

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
    c->startupError.clear();

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
#if defined(AUREA_GPU_GLES)
    // A debug-only override exercises the real fallback through the app's UI,
    // decoder, surface lifecycle and MediaCodec export, without changing release preferences.
    char selectedBackend[PROP_VALUE_MAX]{};
    if (debug == JNI_TRUE) __system_property_get("debug.aurea.gpu", selectedBackend);
    const bool forceGles = std::strcmp(selectedBackend, "gles") == 0;
    config.backend = forceGles ? static_cast<GPUBackend*>(new (std::nothrow) gles::Backend())
                               : static_cast<GPUBackend*>(new (std::nothrow) vk::Backend());
#else
    config.backend = new (std::nothrow) vk::Backend();
#endif
    if (!config.backend) { c->startupError = "Sem memoria para inicializar a GPU"; return JNI_FALSE; }
    config.backendConfig.enableValidation = debug == JNI_TRUE;
    config.backendConfig.enableGpuTimers = true;
    // Device diagnostics only: isolate timestamp-query driver failures without
    // changing the scene, effects, shutter samples or release configuration.
    if (debug == JNI_TRUE) {
        char gpuTimers[PROP_VALUE_MAX]{};
        __system_property_get("debug.aurea.gpu_timers", gpuTimers);
        if (std::strcmp(gpuTimers, "0") == 0) {
            config.backendConfig.enableGpuTimers = false;
            AUREA_LOG_INFO("diagnostico: timestamps de GPU desativados");
        }
    }
    config.cacheDirectory = to_string(env, cacheDir);
    config.documentsDirectory = to_string(env, documentsDir);
    config.displayRefreshRate = refreshRate > 0.0f ? refreshRate : 60.0f;
    config.platformInfo = info;
    config.hasPlatformInfo = hasInfo;
    config.mediaFactory = &c->media;
    config.exportSinkFactory = &android::make_mediacodec_export_sink;
    config.audioOutput = &c->audioOut;
    config.defaultFontPath = config.cacheDirectory + "/Roboto-Regular.ttf";
    // The shared text engine finds the bundled Japanese fallback beside Roboto,
    // exactly as on iOS; it does not depend on the device's system font inventory.
    config.imageLoader = &load_image;
    config.enableTelemetry = true;

    if (const Status s = c->engine.initialize(config); !s.ok()) {
        c->startupError = std::string(s.message()) + ": " + std::string(s.detail());
        AUREA_LOG_ERROR("falha ao inicializar: %s", s.message().data());
        return JNI_FALSE;
    }
    GPUBackend* gpu = c->engine.gpu();
#if defined(AUREA_GPU_GLES)
    if (!gpu && !forceGles) {
        const auto failed = c->engine.read_status();
        const std::string vulkanError = std::string(to_string(failed.lastError)) + ": " + failed.lastErrorDetail;
        AUREA_LOG_WARN("Vulkan indisponivel (%s); tentando OpenGL ES 3.1", vulkanError.c_str());
        c->engine.shutdown();
        config.backend = new (std::nothrow) gles::Backend();
        if (!config.backend) { c->startupError = "Sem memoria para inicializar OpenGL ES"; return JNI_FALSE; }
        const Status fallback = c->engine.initialize(config);
        gpu = c->engine.gpu();
        if (!fallback.ok() || !gpu) {
            const auto failedGles = c->engine.read_status();
            c->startupError = "Vulkan: " + vulkanError + "; OpenGL ES: " +
                (fallback.ok() ? std::string(to_string(failedGles.lastError)) + ": " + failedGles.lastErrorDetail
                               : std::string(fallback.message()) + ": " + std::string(fallback.detail()));
            c->engine.shutdown();
            return JNI_FALSE;
        }
    }
#endif
    if (!gpu) {
        const auto status = c->engine.read_status();
        c->startupError = std::string(to_string(status.lastError)) + ": " + status.lastErrorDetail;
        AUREA_LOG_ERROR("sem GPU utilizavel: o preview nao tem como desenhar");
        c->engine.shutdown();
        return JNI_FALSE;
    }
    // Caminho do vídeo (aurea/platform/AndroidVideoPath.hpp): o GL do DRIVER em
    // todo aparelho cujo backend importa AHardwareBuffer RGBA — Samsung, Mali,
    // Immortalis, PowerVR, Adreno, emulador. O driver converte o YUV (como no
    // app antigo); o Vulkan só amostra RGBA8. Por decoder, se o GL falhar:
    // planos pela CPU (hardware) → decoder de software.
    //
    // O zero-copy antigo (YCbCr externo no Vulkan) está aposentado: era ele o
    // trecho dependente de driver (preview piscando no Immortalis-G715,
    // listras no PowerVR GM9446, crash em Samsung, bytes crus no gfxstream).
    char manufacturer[PROP_VALUE_MAX]{}, sdk[PROP_VALUE_MAX]{};
    __system_property_get("ro.product.manufacturer", manufacturer);
    __system_property_get("ro.build.version.sdk", sdk);
    // Goldfish/gfxstream may advertise RGBA import while its external video
    // texture is black. Use readable software frames in the Android emulator.
    const bool emulatorVideo = running_on_emulator();
    const bool rgbaImport = gpu->capabilities().externalMemoryHardwareBuffer && !emulatorVideo;
    c->media.set_driver_gl(rgbaImport);
    c->media.set_zero_copy(false);
    bool diagnosticSoftwareFallback = false;
    if (debug == JNI_TRUE) {
        char softwareFallback[PROP_VALUE_MAX]{};
        __system_property_get("debug.aurea.force_sw_fallback", softwareFallback);
        diagnosticSoftwareFallback = std::strcmp(softwareFallback, "1") == 0;
    }
    c->media.set_diagnostic_software_fallback(diagnosticSoftwareFallback);
    if (diagnosticSoftwareFallback) AUREA_LOG_INFO("diagnostico: fallback software forcado com selecao de saida de producao");
    bool diagnosticVideoPixels = false;
    if (debug == JNI_TRUE) {
        char videoPixels[PROP_VALUE_MAX]{};
        __system_property_get("debug.aurea.video_pixels", videoPixels);
        diagnosticVideoPixels = std::strcmp(videoPixels, "1") == 0;
    }
    c->media.set_diagnostic_video_pixels(diagnosticVideoPixels);
    if (diagnosticVideoPixels) AUREA_LOG_INFO("diagnostico: pixels do FBO GL observados em PTS 1500000");
    // Sem o GL (backend GLES, sem importação): a regra de antes — Samsung no
    // decoder de software, o resto em planos pela CPU.
    c->media.set_software_only(emulatorVideo || (!rgbaImport && android::needs_software_video(manufacturer, std::atoi(sdk))));
    AUREA_LOG_INFO("video: %s%s", rgbaImport ? "caminho GL do driver (padrao)" : "planos pela CPU",
                   rgbaImport ? "; queda por decoder: planos pela CPU -> decoder de software"
                   : emulatorVideo ? " (decoder de software no emulador)" : " (backend sem importacao de AHardwareBuffer)");
    c->engine.start_render_thread();
    c->initialized = true;
    return JNI_TRUE;
}

/// Rede de segurança do app: o processo anterior morreu por crash nativo com
/// vídeo aberto → planos YUV pela CPU (sem AHardwareBuffer na GPU). Vale para
/// os decoders abertos depois desta chamada.
AUREA_JNI void AUREA_FN(nativeUseReadableVideoPlanes)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return;
    c->media.set_driver_gl(false);
    c->media.set_software_only(true);
    c->media.set_zero_copy(false);
    AUREA_LOG_INFO("video: decoder de software e planos proprios (modo seguro)");
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

AUREA_JNI jstring AUREA_FN(nativeStartupError)(JNIEnv* env, jclass, jlong handle) {
    auto* c = ctx_of(handle);
    return env->NewStringUTF(c ? c->startupError.c_str() : "nativeCreate failed");
}

AUREA_JNI void AUREA_FN(nativeResume)(JNIEnv*, jclass, jlong handle) {
    if (NativeContext* c = ctx_of(handle)) (void)c->engine.resume();
}

// Fase 8B §13: nível de ComponentCallbacks2.onTrimMemory → ordem de despejo do
// motor. Devolve os bytes liberados (caches de CPU + GPU medida no backend).
AUREA_JNI jlong AUREA_FN(nativeTrimMemory)(JNIEnv*, jclass, jlong handle, jint level) {
    NativeContext* c = ctx_of(handle);
    if (!c) return 0;
    return static_cast<jlong>(c->engine.trim_memory(static_cast<i32>(level)).total);
}

// Fase 8B §12/§15: uso e orçamento por categoria de memória do motor, para a
// tela Ajustes › Armazenamento. `out`: [usado, orçamento] × categoria
// (MemoryClass, na ordem do enum). Devolve o número de categorias escritas.
AUREA_JNI jint AUREA_FN(nativeMemoryReport)(JNIEnv* env, jclass, jlong handle, jlongArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out) return 0;
    constexpr jint kClasses = static_cast<jint>(MemoryClass::_Count);
    if (env->GetArrayLength(out) < kClasses * 2) return 0;
    jlong v[kClasses * 2];
    MemoryManager& m = c->engine.memory();
    for (jint i = 0; i < kClasses; ++i) {
        v[i * 2] = static_cast<jlong>(m.used(static_cast<MemoryClass>(i)));
        v[i * 2 + 1] = static_cast<jlong>(m.budget(static_cast<MemoryClass>(i)));
    }
    env->SetLongArrayRegion(out, 0, kClasses * 2, v);
    return kClasses;
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
    NativeContext* c = ctx_of(handle);
    if (!c || width <= 0 || height <= 0) return;
    // Mesmo lock do attach/detach: um surfaceChanged que chega enquanto a
    // janela está sendo solta/trocada não pode redimensionar uma superfície
    // que já não existe (Galaxy S24 FE: SIGSEGV no resize pela thread de render).
    std::lock_guard<std::mutex> lock(c->surfaceMutex);
    if (!c->window) return;
    // Publishes the latest size only; GPU work happens on the render thread.
    (void)c->engine.resize_surface(static_cast<u32>(width), static_cast<u32>(height));
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

AUREA_JNI jint AUREA_FN(nativeQueryTrackCurve)(JNIEnv* env, jclass, jlong handle, jlong layer,
                                                jint property, jint effect, jint param, jint from, jint to, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out) return 0;
    const jsize count = env->GetArrayLength(out);
    jfloat* values = env->GetFloatArrayElements(out, nullptr);
    if (!values) return 0;
    const u32 written = c->engine.query_track_curve(static_cast<u64>(layer), static_cast<u32>(property),
        static_cast<u32>(effect), static_cast<u32>(param), from, to, values, static_cast<u32>(count));
    env->ReleaseFloatArrayElements(out, values, 0);
    return static_cast<jint>(written);
}

AUREA_JNI jboolean AUREA_FN(nativeQueryKeyframeEasing)(JNIEnv* env, jclass, jlong handle, jlong layer,
                                                       jint property, jint effect, jint param, jint time, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 4) return JNI_FALSE;
    // [4] (se couber) = força da bézier, 1..3.
    f32 handles[5];
    u8 power = 1;
    if (!c->engine.query_keyframe_easing(static_cast<u64>(layer), static_cast<u32>(property),
                                        static_cast<u32>(effect), static_cast<u32>(param), time, handles, &power)) return JNI_FALSE;
    handles[4] = static_cast<f32>(power);
    env->SetFloatArrayRegion(out, 0, env->GetArrayLength(out) >= 5 ? 5 : 4, handles);
    return env->ExceptionCheck() ? JNI_FALSE : JNI_TRUE;
}

AUREA_JNI jint AUREA_FN(nativeQueryKeyframes)(JNIEnv* env, jclass, jlong handle, jlong layer, jobject rows,
                                              jint capacity) {
    NativeContext* c = ctx_of(handle);
    auto* out = static_cast<bridge::KeyframeRow*>(buffer_ptr(env, rows));
    if (!c || !out) return 0;
    return static_cast<jint>(c->engine.query_keyframes(static_cast<u64>(layer), out,
                                                       row_capacity<bridge::KeyframeRow>(env, rows, capacity)));
}

/// Devolve (camadas << 32) | total de keyframes; nada é escrito se não coube
/// (ver Engine::query_all_keyframes).
AUREA_JNI jlong AUREA_FN(nativeQueryAllKeyframes)(JNIEnv* env, jclass, jlong handle, jobject index,
                                                 jint layerCapacity, jobject rows, jint capacity) {
    NativeContext* c = ctx_of(handle);
    auto* idx = static_cast<bridge::KeyframeIndexRow*>(buffer_ptr(env, index));
    auto* out = static_cast<bridge::KeyframeRow*>(buffer_ptr(env, rows));
    if (!c || !idx || !out) return 0;
    u32 layers = 0;
    const u32 total = c->engine.query_all_keyframes(idx, row_capacity<bridge::KeyframeIndexRow>(env, index, layerCapacity),
                                                    out, row_capacity<bridge::KeyframeRow>(env, rows, capacity), &layers);
    return (static_cast<jlong>(layers) << 32) | static_cast<jlong>(total);
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
/// Medida do ÚLTIMO render fora da tela (o que a captura de quadro usa).
///
/// Existe porque fora do editor não há prévia viva e o `PerfPOD` sai todo zero —
/// mas a captura RENDERIZA de verdade, e é este o número que sobra. É ele que
/// separa "a CPU está presa" (`prepareMs`, `recordMs`) de "estamos esperando o
/// decoder" (`mediaWaitMs`) de "estamos esperando a GPU" (`gpuWaitMs`, `gpuMs`).
AUREA_JNI jboolean AUREA_FN(nativeReadOffscreenMeasure)(JNIEnv* env, jclass, jlong handle, jdoubleArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 20) return JNI_FALSE;
    const auto m = c->engine.last_offscreen_measure();
    const jdouble v[20] = {
        m.prepareMs, m.mediaWaitMs, static_cast<jdouble>(m.mediaAttempts), m.recordMs, m.submitMs,
        m.gpuWaitMs, m.gpuMs, m.gpuMeasured ? 1.0 : 0.0, static_cast<jdouble>(m.gpuPasses),
        static_cast<jdouble>(m.passesExecuted), static_cast<jdouble>(m.passesCulled),
        static_cast<jdouble>(m.drawCalls), static_cast<jdouble>(m.layersRendered),
        static_cast<jdouble>(m.draws3D), static_cast<jdouble>(m.triangles3D),
        static_cast<jdouble>(m.culled3D), static_cast<jdouble>(m.particles),
        static_cast<jdouble>(m.activeEffects), static_cast<jdouble>(m.transientBytes),
        static_cast<jdouble>(m.gpuUsedBytes)};
    env->SetDoubleArrayRegion(out, 0, 20, v);
    return JNI_TRUE;
}

/// Liga as timestamp queries do render fora da tela (só a medição usa).
AUREA_JNI jboolean AUREA_FN(nativeOffscreenTimers)(JNIEnv*, jclass, jlong handle, jboolean on) {
    NativeContext* c = ctx_of(handle);
    if (!c) return JNI_FALSE;
    c->engine.set_offscreen_timers(on == JNI_TRUE);
    return JNI_TRUE;
}

// Use the authoritative double FPS and the shared frame round-trip correction.
AUREA_JNI jlong AUREA_FN(nativeFrameTimeNs)(JNIEnv*, jclass, jlong handle, jlong frame) {
    NativeContext* c = ctx_of(handle);
    if (!c) return 0;
    u64 id = 0;
    u32 w = 0, h = 0;
    f64 fps = 0.0;
    i64 duration = 0;
    f32 background[4]{};
    if (!c->engine.query_composition(id, w, h, fps, duration, background)) return 0;
    return static_cast<jlong>(tick_at(FrameIndex{static_cast<i64>(frame)}, fps).value);
}

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
    const Status captured = c->engine.capture_frame_rgba(static_cast<u32>(maxDim), rgba, w, h);
    if (!captured.ok()) {
        AUREA_LOG_WARN("captura do projeto falhou (%u): %s", static_cast<u32>(captured.code()), captured.message().data());
        return 0;
    }
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

AUREA_JNI jint AUREA_FN(nativeCopyEffects)(JNIEnv*, jclass, jlong handle, jlong layer, jint effect) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.copy_effects(static_cast<u64>(layer),static_cast<u32>(effect))) : 0;
}

AUREA_JNI jboolean AUREA_FN(nativeCopyTransform)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.copy_transform(static_cast<u64>(layer)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativePasteTransform)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    NativeContext* c = ctx_of(handle);
    const auto values = jlongs(env, ids);
    return c ? static_cast<jint>(c->engine.paste_transform(values.data(), static_cast<u32>(values.size()))) : 0;
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

AUREA_JNI jint AUREA_FN(nativeKeyframeSelection)(JNIEnv* env, jclass, jlong handle, jlong layer, jlongArray references, jint action, jint delta) {
    NativeContext* c = ctx_of(handle);
    if (!c || !references || action < 0 || action > 2) return 0;
    const jsize length = env->GetArrayLength(references);
    if (length <= 0 || length % 4 != 0 || length > 16384 * 4) return 0;
    std::vector<jlong> packed(length);
    env->GetLongArrayRegion(references, 0, length, packed.data());
    std::vector<i64> refs(packed.begin(), packed.end());
    return action == 0 ? c->engine.copy_keyframe_selection(static_cast<u64>(layer), refs.data(), length / 4)
        : c->engine.edit_keyframe_selection(static_cast<u64>(layer), refs.data(), length / 4, delta, action == 2);
}

AUREA_JNI jint AUREA_FN(nativePasteKeyframes)(JNIEnv* env, jclass, jlong handle, jlongArray ids, jlong frame) {
    NativeContext* c = ctx_of(handle);
    const auto v = jlongs(env, ids);
    return c ? static_cast<jint>(c->engine.paste_keyframes(v.data(), static_cast<u32>(v.size()), frame)) : 0;
}

AUREA_JNI jint AUREA_FN(nativeCopyAnimation)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.copy_animation(static_cast<u64>(layer))) : 0;
}

AUREA_JNI jint AUREA_FN(nativeOptimizeKeyframes)(JNIEnv*, jclass, jlong handle, jlong layer, jint property, jfloat tolerance) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.optimize_keyframes(static_cast<u64>(layer), property, tolerance)) : 0;
}

AUREA_JNI jint AUREA_FN(nativeClipboardState)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.clipboard_state()) : 0;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryGizmo)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloat length, jfloatArray out, jboolean localSpace) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 8) return JNI_FALSE;
    f32 v[8];
    if (!c->engine.query_gizmo(static_cast<u64>(layer), length, v, localSpace == JNI_TRUE)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 8, v);
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryTrackball)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    constexpr jsize n = static_cast<jsize>(aurea::trackball::kQueryFloats);
    if (!c || !out || env->GetArrayLength(out) < n) return JNI_FALSE;
    f32 v[aurea::trackball::kQueryFloats]{};
    if (!c->engine.query_trackball(static_cast<u64>(layer), v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, n, v);
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativePreviewGestureBasis)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    auto* c = ctx_of(handle); f32 values[13]{};
    if (!c || !out || env->GetArrayLength(out) < 13 || !c->engine.query_preview_gesture_basis(static_cast<u64>(layer), values)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 13, values); return JNI_TRUE;
}
AUREA_JNI jfloatArray AUREA_FN(previewGestureValue)(JNIEnv* env, jclass, jfloatArray basis, jfloat dx, jfloat dy, jboolean rotate) {
    if (!basis || env->GetArrayLength(basis) != 13) return env->NewFloatArray(0);
    f32 values[13]{}; env->GetFloatArrayRegion(basis, 0, 13, values);
    const auto v = aurea::preview_gesture_value(values, dx, dy, rotate == JNI_TRUE);
    const f32 out[]{v.x,v.y,v.z}; auto result = env->NewFloatArray(3);
    if (result) env->SetFloatArrayRegion(result,0,3,out); return result;
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

AUREA_JNI jboolean AUREA_FN(nativeSetEnvironmentBackground)(JNIEnv*, jclass, jlong handle, jboolean visible) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_environment_background(visible == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}
AUREA_JNI jboolean AUREA_FN(nativeSetEnvironment)(JNIEnv*, jclass, jlong handle, jfloat intensity, jfloat rotation) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_environment_params(intensity, rotation) ? JNI_TRUE : JNI_FALSE;
}
AUREA_JNI jboolean AUREA_FN(nativeSetEnvironmentBackgroundRange)(JNIEnv*, jclass, jlong handle, jlong start, jlong end) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_environment_background_range(start, end) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryEnvironment)(JNIEnv* env, jclass, jlong handle, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 3) return JNI_FALSE;
    f32 v[3];
    if (!c->engine.query_environment(v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 3, v);
    if (env->GetArrayLength(out) >= 4) { const f32 visible = c->engine.environment_background() ? 1.f : 0.f; env->SetFloatArrayRegion(out, 3, 1, &visible); }
    if (env->GetArrayLength(out) >= 6) {
        const f32 range[2]{static_cast<f32>(c->engine.environment_background_start()), static_cast<f32>(c->engine.environment_background_end())};
        env->SetFloatArrayRegion(out, 4, 2, range);
    }
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetObjectEnvironment)(JNIEnv*, jclass, jlong handle, jlong layer, jint source,
                                                        jlong hdri, jfloat intensity, jfloat rotation, jfloat exposure) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_object_environment(static_cast<u64>(layer), static_cast<u32>(source),
                                                 static_cast<u64>(hdri), intensity, rotation, exposure)
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryObjectEnvironment)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 5) return JNI_FALSE;
    f32 v[5];
    if (!c->engine.query_object_environment(static_cast<u64>(layer), v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 5, v);
    return JNI_TRUE;
}

AUREA_JNI jfloatArray AUREA_FN(nativeQueryMaterials)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    const u32 count = c ? std::min<u32>(4096, c->engine.query_materials(static_cast<u64>(layer), nullptr, 0)) : 0;
    std::vector<f32> values(static_cast<usize>(count) * 8);
    const u32 written = count ? std::min(count, c->engine.query_materials(static_cast<u64>(layer), values.data(), count)) : 0;
    jfloatArray out = env->NewFloatArray(static_cast<jsize>(written * 8));
    if (out && written) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(written * 8), values.data());
    return out;
}

AUREA_JNI jboolean AUREA_FN(nativeSetMaterialParam)(JNIEnv*, jclass, jlong handle, jlong layer, jint material, jint param, jfloat value) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_material_param(static_cast<u64>(layer), static_cast<u32>(material), static_cast<u32>(param), value).ok()
        ? JNI_TRUE : JNI_FALSE;
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

AUREA_JNI jboolean AUREA_FN(nativeSetMaskParam)(JNIEnv*, jclass, jlong handle, jlong layer, jint mask, jint param, jfloat value) {
    NativeContext* c = ctx_of(handle);
    return c && mask >= 0 && param >= 0 && c->engine.set_mask_param(static_cast<u64>(layer), static_cast<u32>(mask), static_cast<u32>(param), value) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeToggleMaskParamKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint mask, jint param) {
    NativeContext* c = ctx_of(handle);
    return c && mask >= 0 && param >= 0 && c->engine.toggle_mask_param_key(static_cast<u64>(layer), static_cast<u32>(mask), static_cast<u32>(param)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeToggleMaskPathKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint mask) {
    NativeContext* c = ctx_of(handle);
    bool keyed = false;
    // "Marcar keyframe" do painel da máscara: regrava a forma do instante se já
    // houver keyframe aqui (apagar é pelo menu do keyframe, na timeline).
    if (!c || mask < 0 || !c->engine.ensure_mask_path_key(static_cast<u64>(layer), static_cast<u32>(mask), &keyed)) return -1;
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

// --- Rig 2D (Engine::query_rig e família) ------------------------------------
AUREA_JNI jint AUREA_FN(nativeQueryRig)(JNIEnv* env, jclass, jlong handle, jlong layer, jboolean bind, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c) return 0;
    const jsize cap = out ? env->GetArrayLength(out) : 0;
    std::vector<f32> v(static_cast<usize>(cap));
    const u32 need = c->engine.query_rig(static_cast<u64>(layer), bind == JNI_TRUE, cap ? v.data() : nullptr, static_cast<u32>(cap));
    if (need && need <= static_cast<u32>(cap)) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(need), v.data());
    return static_cast<jint>(need);
}

AUREA_JNI jint AUREA_FN(nativeRigAddJoint)(JNIEnv*, jclass, jlong handle, jlong layer, jint parent, jfloat x, jfloat y) {
    NativeContext* c = ctx_of(handle);
    return c ? c->engine.rig_add_joint(static_cast<u64>(layer), parent, x, y) : -1;
}

AUREA_JNI jboolean AUREA_FN(nativeRigMoveJoint)(JNIEnv*, jclass, jlong handle, jlong layer, jint joint, jfloat x, jfloat y,
                                                jboolean continuing) {
    NativeContext* c = ctx_of(handle);
    return c && joint >= 0 && c->engine.rig_move_joint(static_cast<u64>(layer), static_cast<u32>(joint), x, y, continuing == JNI_TRUE)
        ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeRigRemoveJoint)(JNIEnv*, jclass, jlong handle, jlong layer, jint joint) {
    NativeContext* c = ctx_of(handle);
    return c && joint >= 0 && c->engine.rig_remove_joint(static_cast<u64>(layer), static_cast<u32>(joint)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeRigClear)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.rig_clear(static_cast<u64>(layer)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeRigAutoHumanoid)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.rig_auto_humanoid(static_cast<u64>(layer))) : 0;
}

AUREA_JNI jboolean AUREA_FN(nativeRigPoseJoint)(JNIEnv*, jclass, jlong handle, jlong layer, jint joint, jfloat x, jfloat y,
                                                jboolean continuing) {
    NativeContext* c = ctx_of(handle);
    return c && joint >= 0 && c->engine.rig_pose_joint(static_cast<u64>(layer), static_cast<u32>(joint), x, y, continuing == JNI_TRUE)
        ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI void AUREA_FN(nativeSetRigSetupLayer)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (c) c->engine.set_rig_setup_layer(static_cast<u64>(layer));
}

// --- Malha de deformação (Engine::query_mesh_warp e família) ------------------
AUREA_JNI jint AUREA_FN(nativeQueryMeshWarp)(JNIEnv* env, jclass, jlong handle, jlong layer, jint effect, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || effect < 0) return 0;
    const jsize cap = out ? env->GetArrayLength(out) : 0;
    std::vector<f32> v(static_cast<usize>(cap));
    const u32 need = c->engine.query_mesh_warp(static_cast<u64>(layer), static_cast<u32>(effect), cap ? v.data() : nullptr,
                                               static_cast<u32>(cap));
    if (need && need <= static_cast<u32>(cap)) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(need), v.data());
    return static_cast<jint>(need);
}

AUREA_JNI jboolean AUREA_FN(nativeMeshWarpDrag)(JNIEnv*, jclass, jlong handle, jlong layer, jint effect, jint vertex, jint grip,
                                                jfloat u, jfloat v, jboolean autoKey, jboolean continuing) {
    NativeContext* c = ctx_of(handle);
    return c && effect >= 0 && vertex >= 0 && grip >= 0
        && c->engine.mesh_warp_drag(static_cast<u64>(layer), static_cast<u32>(effect), static_cast<u32>(vertex),
                                    static_cast<u32>(grip), u, v, autoKey == JNI_TRUE, continuing == JNI_TRUE)
        ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeMeshWarpReset)(JNIEnv*, jclass, jlong handle, jlong layer, jint effect) {
    NativeContext* c = ctx_of(handle);
    return c && effect >= 0 && c->engine.mesh_warp_reset(static_cast<u64>(layer), static_cast<u32>(effect)) ? JNI_TRUE : JNI_FALSE;
}

// --- Fantoche (Engine::query_puppet e família; u, v = fração da camada) -------
static jint puppet_query(JNIEnv* env, jfloatArray out, u32 (*fn)(NativeContext*, u64, u32, f32*, u32), NativeContext* c,
                         jlong layer, jint effect) {
    if (!c || effect < 0) return 0;
    const jsize cap = out ? env->GetArrayLength(out) : 0;
    std::vector<f32> v(static_cast<usize>(cap));
    const u32 need = fn(c, static_cast<u64>(layer), static_cast<u32>(effect), cap ? v.data() : nullptr, static_cast<u32>(cap));
    if (need && need <= static_cast<u32>(cap)) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(need), v.data());
    return static_cast<jint>(need);
}

AUREA_JNI jint AUREA_FN(nativeQueryPuppet)(JNIEnv* env, jclass, jlong handle, jlong layer, jint effect, jfloatArray out) {
    return puppet_query(env, out, [](NativeContext* c, u64 l, u32 e, f32* o, u32 n) { return c->engine.query_puppet(l, e, o, n); },
                        ctx_of(handle), layer, effect);
}

AUREA_JNI jint AUREA_FN(nativeQueryPuppetMesh)(JNIEnv* env, jclass, jlong handle, jlong layer, jint effect, jfloatArray out) {
    return puppet_query(env, out, [](NativeContext* c, u64 l, u32 e, f32* o, u32 n) { return c->engine.query_puppet_mesh(l, e, o, n); },
                        ctx_of(handle), layer, effect);
}

AUREA_JNI jint AUREA_FN(nativePuppetAddPin)(JNIEnv*, jclass, jlong handle, jlong layer, jint effect, jfloat u, jfloat v) {
    NativeContext* c = ctx_of(handle);
    return c && effect >= 0 ? c->engine.puppet_add_pin(static_cast<u64>(layer), static_cast<u32>(effect), u, v) : -1;
}

AUREA_JNI jboolean AUREA_FN(nativePuppetMovePin)(JNIEnv*, jclass, jlong handle, jlong layer, jint effect, jint pin, jfloat u, jfloat v,
                                                 jboolean autoKey, jboolean continuing) {
    NativeContext* c = ctx_of(handle);
    return c && effect >= 0 && pin >= 0
        && c->engine.puppet_move_pin(static_cast<u64>(layer), static_cast<u32>(effect), static_cast<u32>(pin), u, v,
                                     autoKey == JNI_TRUE, continuing == JNI_TRUE)
        ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativePuppetRemovePin)(JNIEnv*, jclass, jlong handle, jlong layer, jint effect, jint pin) {
    NativeContext* c = ctx_of(handle);
    return c && effect >= 0 && pin >= 0
        && c->engine.puppet_remove_pin(static_cast<u64>(layer), static_cast<u32>(effect), static_cast<u32>(pin)) ? JNI_TRUE : JNI_FALSE;
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
/// Receita do texto 3D, campo a campo. O Kotlin manda e recebe os números num
/// FloatArray só: a ordem é o contrato entre os dois lados (kText3DFields).
constexpr jint kText3DFields = 35;

void write_text3d(const aurea::scene3d::Text3DSpec& s, f32* v) {
    v[0] = s.depth;
    v[1] = static_cast<f32>(s.alignment);
    v[2] = s.color.x; v[3] = s.color.y; v[4] = s.color.z;
    v[5] = s.bevel ? 1.0f : 0.0f;
    v[6] = s.bevelWidth;
    v[7] = s.bevelDepth;
    v[8] = static_cast<f32>(s.bevelSegments);
    v[9] = s.bevelRoundness;
    v[10] = s.metallic; v[11] = s.roughness; v[12] = s.specular; v[13] = s.occlusion;
    v[14] = s.emissive.x; v[15] = s.emissive.y; v[16] = s.emissive.z;
    v[17] = s.emissiveStrength;
    v[18] = s.regionMaterials ? 1.0f : 0.0f;
    v[19] = s.side.color.x; v[20] = s.side.color.y; v[21] = s.side.color.z;
    v[22] = s.side.metallic; v[23] = s.side.roughness;
    v[24] = s.bevelMat.color.x; v[25] = s.bevelMat.color.y; v[26] = s.bevelMat.color.z;
    v[27] = s.bevelMat.metallic; v[28] = s.bevelMat.roughness;
    v[29] = static_cast<f32>(s.animation); v[30] = s.animationDuration;
    v[31] = s.animationStagger; v[32] = s.animationAmount;
    v[33] = static_cast<f32>(s.surfaceFinish); v[34] = s.separateGlyphs ? 1.f : 0.f;
}

bool read_text3d(JNIEnv* env, jstring content, jfloatArray p, aurea::scene3d::Text3DSpec& s) {
    const char* text = env->GetStringUTFChars(content, nullptr);
    s.content = text ? text : "";
    if (text) env->ReleaseStringUTFChars(content, text);
    if (!p || env->GetArrayLength(p) < kText3DFields) return false;
    f32 v[kText3DFields]{};
    env->GetFloatArrayRegion(p, 0, kText3DFields, v);
    s.depth = v[0];
    s.alignment = static_cast<u32>(std::max(0.0f, v[1]));
    s.color = Vec4{v[2], v[3], v[4], 1.0f};
    s.bevel = v[5] >= 0.5f;
    s.bevelWidth = v[6];
    s.bevelDepth = v[7];
    s.bevelSegments = static_cast<u32>(std::clamp(v[8], 1.0f, 8.0f));
    s.bevelRoundness = v[9];
    s.metallic = v[10]; s.roughness = v[11]; s.specular = v[12]; s.occlusion = v[13];
    s.emissive = Vec3{v[14], v[15], v[16]};
    s.emissiveStrength = v[17];
    s.regionMaterials = v[18] >= 0.5f;
    s.side.color = Vec4{v[19], v[20], v[21], 1.0f};
    s.side.metallic = v[22]; s.side.roughness = v[23];
    s.bevelMat.color = Vec4{v[24], v[25], v[26], 1.0f};
    s.bevelMat.metallic = v[27]; s.bevelMat.roughness = v[28];
    s.animation = static_cast<u32>(std::clamp(v[29], 0.f, 4.f));
    s.animationDuration = std::clamp(v[30], .2f, 30.f);
    s.animationStagger = std::clamp(v[31], 0.f, 1.f);
    s.animationAmount = std::clamp(v[32], 0.f, 2.f);
    s.surfaceFinish = static_cast<u32>(std::clamp(v[33], 0.f, 4.f)); s.separateGlyphs = v[34] >= .5f;
    return true;
}
} // namespace

AUREA_JNI jlong AUREA_FN(nativeAddText3d)(JNIEnv* env, jclass, jlong handle, jstring content, jfloatArray p, jstring fontPath) {
    NativeContext* c = ctx_of(handle);
    if (!c || !content) return -static_cast<jlong>(Errc::InvalidState);
    aurea::scene3d::Text3DSpec s;
    if (!read_text3d(env, content, p, s)) return -static_cast<jlong>(Errc::InvalidArgument);
    if (fontPath) { const char* v = env->GetStringUTFChars(fontPath, nullptr); if (v) { s.fontPath = v; env->ReleaseStringUTFChars(fontPath, v); } }
    const Result<u64> res = c->engine.add_text3d(s);
    if (!res.ok()) return -static_cast<jlong>(res.status().code());
    return static_cast<jlong>(*res);
}

AUREA_JNI jboolean AUREA_FN(nativeSetText3d)(JNIEnv* env, jclass, jlong handle, jlong layer, jstring content, jfloatArray p, jstring fontPath) {
    NativeContext* c = ctx_of(handle);
    if (!c || !content) return JNI_FALSE;
    aurea::scene3d::Text3DSpec s;
    // Preserve region emission and other recipe fields not exposed in the UI array.
    if (!c->engine.query_text3d(static_cast<u64>(layer), s)) return JNI_FALSE;
    if (!read_text3d(env, content, p, s)) return JNI_FALSE;
    if (fontPath) { const char* v = env->GetStringUTFChars(fontPath, nullptr); if (v) { s.fontPath = v; env->ReleaseStringUTFChars(fontPath, v); } }
    return c->engine.set_text3d(static_cast<u64>(layer), s).ok() ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeApplyText3dPreset)(JNIEnv*, jclass, jlong handle, jlong layer, jint preset) {
    NativeContext* c = ctx_of(handle);
    aurea::scene3d::Text3DSpec s;
    if (!c || !c->engine.query_text3d(static_cast<u64>(layer), s) ||
        !aurea::scene3d::apply_text3d_material_preset(s, static_cast<u32>(preset))) return JNI_FALSE;
    return c->engine.set_text3d(static_cast<u64>(layer), s).ok() ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetText3dTexture)(JNIEnv* env, jclass, jlong handle, jlong layer, jstring path) {
    NativeContext* c = ctx_of(handle);
    if (!c || !path) return JNI_FALSE;
    const char* value = env->GetStringUTFChars(path, nullptr);
    if (!value) return JNI_FALSE;
    const std::string file(value);
    env->ReleaseStringUTFChars(path, value);
    return c->engine.set_text3d_texture(static_cast<u64>(layer), file).ok() ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jstring AUREA_FN(nativeQueryText3dTexture)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    aurea::scene3d::Text3DSpec spec;
    if (!c || !c->engine.query_text3d(static_cast<u64>(layer), spec)) return nullptr;
    return env->NewStringUTF(spec.texturePath.c_str());
}

/// Receita do texto 3D: devolve o texto (nulo = não é texto 3D) e preenche o
/// FloatArray com os 29 campos de `write_text3d`.
AUREA_JNI jstring AUREA_FN(nativeQueryText3d)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < kText3DFields) return nullptr;
    aurea::scene3d::Text3DSpec s;
    if (!c->engine.query_text3d(static_cast<u64>(layer), s)) return nullptr;
    f32 v[kText3DFields]{};
    write_text3d(s, v);
    env->SetFloatArrayRegion(out, 0, kText3DFields, v);
    return env->NewStringUTF(s.content.c_str());
}

/// Sombras do objeto 3D: projeta / recebe.
AUREA_JNI jstring AUREA_FN(nativeQueryText3dFont)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    aurea::scene3d::Text3DSpec s;
    if (!c || !c->engine.query_text3d(static_cast<u64>(layer), s)) return nullptr;
    return env->NewStringUTF(s.fontPath.c_str());
}

AUREA_JNI jboolean AUREA_FN(nativeSetModelShadows)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean cast, jboolean receive) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_model_shadows(static_cast<u64>(layer), cast == JNI_TRUE, receive == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryModelShadows)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 2) return JNI_FALSE;
    f32 v[2]{};
    if (!c->engine.query_model_shadows(static_cast<u64>(layer), v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 2, v);
    return JNI_TRUE;
}

// --- Mostrar interior e miniatura de material (EngineShape3D.cpp) -----------------
AUREA_JNI jboolean AUREA_FN(nativeSetModelInterior)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_model_interior(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeQueryModelInterior)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.query_model_interior(static_cast<u64>(layer))) : -1;
}

namespace {
/// RGBA8 (alfa reto) → ARGB por int, o formato de Bitmap.createBitmap(int[]).
jintArray argb_array(JNIEnv* env, const std::vector<u8>& rgba) {
    const jsize n = static_cast<jsize>(rgba.size() / 4u);
    jintArray out = n > 0 ? env->NewIntArray(n) : nullptr;
    if (!out) return nullptr;
    std::vector<jint> px(static_cast<usize>(n));
    for (jsize i = 0; i < n; ++i) {
        const u8* p = &rgba[static_cast<usize>(i) * 4u];
        px[static_cast<usize>(i)] = static_cast<jint>((static_cast<u32>(p[3]) << 24) | (static_cast<u32>(p[0]) << 16)
                                                      | (static_cast<u32>(p[1]) << 8) | p[2]);
    }
    env->SetIntArrayRegion(out, 0, n, px.data());
    return out;
}
} // namespace

AUREA_JNI jintArray AUREA_FN(nativeMaterialPreview)(JNIEnv* env, jclass, jlong handle, jlong layer, jint material, jint size) {
    NativeContext* c = ctx_of(handle);
    std::vector<u8> rgba;
    if (!c || material < 0 || size <= 0
        || !c->engine.material_preview(static_cast<u64>(layer), static_cast<u32>(material), static_cast<u32>(size), rgba)) return nullptr;
    return argb_array(env, rgba);
}

AUREA_JNI jintArray AUREA_FN(nativeText3DPresetPreview)(JNIEnv* env, jclass, jlong handle, jint preset, jint size) {
    NativeContext* c = ctx_of(handle);
    std::vector<u8> rgba;
    if (!c || preset < 0 || size <= 0
        || !c->engine.text3d_preset_preview(static_cast<u32>(preset), static_cast<u32>(size), rgba)) return nullptr;
    return argb_array(env, rgba);
}

// --- Formas 3D (Engine::add_shape3d e família; scene3d/Shape3D.hpp) -----------
AUREA_JNI jlong AUREA_FN(nativeAddShape3d)(JNIEnv* env, jclass, jlong handle, jint kind, jstring name) {
    NativeContext* c = ctx_of(handle);
    if (!c || kind < 0) return -static_cast<jlong>(Errc::InvalidState);
    std::string label;
    if (name) { const char* v = env->GetStringUTFChars(name, nullptr); if (v) { label = v; env->ReleaseStringUTFChars(name, v); } }
    const Result<u64> r = c->engine.add_shape3d(static_cast<u32>(kind), label.c_str());
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

/// Receita: out[0] forma, out[1] nº de partes, depois 5 por parte (RGBA sRGB,
/// 1 = tem imagem). Devolve o nº de partes (−1 = não é forma 3D).
AUREA_JNI jint AUREA_FN(nativeQueryShape3d)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    constexpr jsize kFloats = 2 + 5 * static_cast<jsize>(aurea::scene3d::kShape3DMaxParts);
    if (!c || !out || env->GetArrayLength(out) < kFloats) return -1;
    aurea::scene3d::Shape3DSpec s;
    if (!c->engine.query_shape3d(static_cast<u64>(layer), s)) return -1;
    f32 v[kFloats]{};
    v[0] = static_cast<f32>(static_cast<u32>(s.kind));
    v[1] = static_cast<f32>(s.parts.size());
    for (usize i = 0; i < s.parts.size() && i < aurea::scene3d::kShape3DMaxParts; ++i) {
        f32* o = v + 2 + i * 5;
        o[0] = s.parts[i].color.x; o[1] = s.parts[i].color.y; o[2] = s.parts[i].color.z; o[3] = s.parts[i].color.w;
        o[4] = s.parts[i].image.empty() ? 0.0f : 1.0f;
    }
    env->SetFloatArrayRegion(out, 0, kFloats, v);
    return static_cast<jint>(s.parts.size());
}

/// Cor (nula = mantém) e imagem (nula = mantém, "" = tira) da parte (−1 = todas).
AUREA_JNI jboolean AUREA_FN(nativeSetShape3dPartStyle)(JNIEnv* env, jclass, jlong handle, jlong layer, jint part, jfloatArray rgba,
                                                       jstring image) {
    NativeContext* c = ctx_of(handle);
    if (!c) return JNI_FALSE;
    f32 color[4]{};
    const bool hasColor = rgba && env->GetArrayLength(rgba) >= 4;
    if (hasColor) env->GetFloatArrayRegion(rgba, 0, 4, color);
    std::string path;
    if (image) { const char* v = env->GetStringUTFChars(image, nullptr); if (v) { path = v; env->ReleaseStringUTFChars(image, v); } }
    return c->engine.set_shape3d_part_style(static_cast<u64>(layer), part, hasColor ? color : nullptr, image ? path.c_str() : nullptr).ok()
        ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeQueryShape3dParts)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c) return 0;
    const jsize cap = out ? env->GetArrayLength(out) : 0;
    std::vector<f32> v(static_cast<usize>(cap));
    const u32 need = c->engine.query_shape3d_parts(static_cast<u64>(layer), cap ? v.data() : nullptr, static_cast<u32>(cap));
    if (need && need <= static_cast<u32>(cap)) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(need), v.data());
    return static_cast<jint>(need);
}

AUREA_JNI jboolean AUREA_FN(nativeSetShape3dPart)(JNIEnv* env, jclass, jlong handle, jlong layer, jint part, jfloatArray values,
                                                  jint mask, jboolean continuing) {
    NativeContext* c = ctx_of(handle);
    if (!c || part < 0 || !values || env->GetArrayLength(values) < 9) return JNI_FALSE;
    f32 v[9];
    env->GetFloatArrayRegion(values, 0, 9, v);
    return c->engine.set_shape3d_part(static_cast<u64>(layer), static_cast<u32>(part), v, static_cast<u32>(mask), continuing == JNI_TRUE)
        ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeToggleShape3dPartKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint part) {
    NativeContext* c = ctx_of(handle);
    return c && part >= 0 ? c->engine.toggle_shape3d_part_key(static_cast<u64>(layer), static_cast<u32>(part)) : -1;
}

AUREA_JNI jboolean AUREA_FN(nativeResetShape3dPart)(JNIEnv*, jclass, jlong handle, jlong layer, jint part) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.reset_shape3d_part(static_cast<u64>(layer), part) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryShape3dPartGizmo)(JNIEnv* env, jclass, jlong handle, jlong layer, jint part, jfloat length,
                                                         jfloatArray out, jboolean localSpace) {
    NativeContext* c = ctx_of(handle);
    if (!c || part < 0 || !out || env->GetArrayLength(out) < 8) return JNI_FALSE;
    f32 v[8];
    if (!c->engine.query_shape3d_part_gizmo(static_cast<u64>(layer), static_cast<u32>(part), length, v, localSpace == JNI_TRUE)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 8, v);
    return JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeShape3dPartMove)(JNIEnv* env, jclass, jlong handle, jlong layer, jint part, jint axis, jfloat amount,
                                                   jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || part < 0 || axis < 0 || !out || env->GetArrayLength(out) < 3) return JNI_FALSE;
    f32 v[3];
    if (!c->engine.shape3d_part_move(static_cast<u64>(layer), static_cast<u32>(part), static_cast<u32>(axis), amount, v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 3, v);
    return JNI_TRUE;
}

/// Divide o cubo em [count] fatias no eixo [axis] (0 X, 1 Y, 2 Z). Devolve o
/// id do nulo do grupo (o mesmo da camada), ou −Errc.
AUREA_JNI jlong AUREA_FN(nativeSplitShape3d)(JNIEnv*, jclass, jlong handle, jlong layer, jint axis, jint count) {
    NativeContext* c = ctx_of(handle);
    if (!c || axis < 0 || count < 0) return -static_cast<jlong>(Errc::InvalidArgument);
    const Result<u64> r = c->engine.split_shape3d(static_cast<u64>(layer), static_cast<u32>(axis), static_cast<u32>(count));
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
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
    // Um slot por ParticleParam: o tamanho e o contrato, nao um numero solto.
    constexpr jsize kSlots = static_cast<jsize>(ParticleParam::Count);
    if (!c || !out || env->GetArrayLength(out) < kSlots) return JNI_FALSE;
    f32 v[kSlots];
    if (!c->engine.query_particles(static_cast<u64>(layer), v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, kSlots, v);
    return JNI_TRUE;
}

// --- Aurea Particular 8.2: fonte, textura, malha e curvas ao longo da vida ---
AUREA_JNI jboolean AUREA_FN(nativeSetParticleSource)(JNIEnv*, jclass, jlong handle, jlong layer, jlong source) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_particle_source(static_cast<u64>(layer), static_cast<u64>(source)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetParticleTexture)(JNIEnv*, jclass, jlong handle, jlong layer, jlong image) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_particle_texture(static_cast<u64>(layer), static_cast<u64>(image)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetParticleMesh)(JNIEnv*, jclass, jlong handle, jlong layer, jlong model) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_particle_mesh(static_cast<u64>(layer), static_cast<u64>(model)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetParticleLifeCurve)(JNIEnv* env, jclass, jlong handle, jlong layer, jint kind,
                                                        jfloatArray values, jint count) {
    NativeContext* c = ctx_of(handle);
    if (!c || kind < 0 || kind > 2 || count < 0) return JNI_FALSE;
    const jsize per = kind == 0 ? 4 : 2;
    const jsize n = std::min<jsize>(count, static_cast<jsize>(ParticleData::kMaxLifeStops));
    f32 v[ParticleData::kMaxLifeStops * 4]{};
    if (n > 0) {
        if (!values || env->GetArrayLength(values) < n * per) return JNI_FALSE;
        env->GetFloatArrayRegion(values, 0, n * per, v);
    }
    return c->engine.set_particle_life_curves(static_cast<u64>(layer), static_cast<u32>(kind), v, static_cast<u32>(n))
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryParticleLinks)(JNIEnv* env, jclass, jlong handle, jlong layer, jlongArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 4) return JNI_FALSE;
    u64 v[4]{};
    if (!c->engine.query_particle_links(static_cast<u64>(layer), v)) return JNI_FALSE;
    const jlong j[4] = {static_cast<jlong>(v[0]), static_cast<jlong>(v[1]), static_cast<jlong>(v[2]), static_cast<jlong>(v[3])};
    env->SetLongArrayRegion(out, 0, 4, j);
    return JNI_TRUE;
}

AUREA_JNI jint AUREA_FN(nativeQueryParticleCurve)(JNIEnv* env, jclass, jlong handle, jlong layer, jint kind, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || kind < 0 || kind > 2) return 0;
    f32 v[ParticleData::kMaxLifeStops * 4]{};
    const u32 cap = static_cast<u32>(std::min<jsize>(env->GetArrayLength(out), static_cast<jsize>(ParticleData::kMaxLifeStops * 4)));
    const u32 n = c->engine.query_particle_curve(static_cast<u64>(layer), static_cast<u32>(kind), v, cap);
    const jsize per = kind == 0 ? 4 : 2;
    if (n > 0) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(n) * per, v);
    return static_cast<jint>(n);
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
/// A tradução mora no núcleo (`thermal_state_from_android`, testada no host):
/// 0 NONE → NORMAL · 1 LIGHT → WARM · 2 MODERATE, 3 SEVERE → HOT ·
/// 4 CRITICAL e acima → CRITICAL.
AUREA_JNI void AUREA_FN(nativeSetThermal)(JNIEnv*, jclass, jlong handle, jint status) {
    NativeContext* c = ctx_of(handle);
    if (!c) return;
    const ThermalState t = thermal_state_from_android(static_cast<i32>(status));
    c->engine.set_thermal(static_cast<u32>(t.level), t.throttling);
}

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

/// O que o motor decidiu para ESTE aparelho, em números.
///
/// A UI mostra o que foi decidido — não um "otimizado!" sem lastro. O layout
/// dos slots é o de `write_device_report` (DeviceCapabilities.hpp), o mesmo
/// que o `DeviceReport.kt` lê; um array menor que ele é recusado.
AUREA_JNI jboolean AUREA_FN(nativeDeviceReport)(JNIEnv* env, jclass, jlong handle, jlongArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out) return JNI_FALSE;
    if (env->GetArrayLength(out) < static_cast<jsize>(kDeviceReportSlots)) return JNI_FALSE;
    i64 report[kDeviceReportSlots];
    write_device_report(c->engine.caps(), report);
    jlong copy[kDeviceReportSlots];
    for (u32 i = 0; i < kDeviceReportSlots; ++i) copy[i] = static_cast<jlong>(report[i]);
    env->SetLongArrayRegion(out, 0, static_cast<jsize>(kDeviceReportSlots), copy);
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

AUREA_JNI jint AUREA_FN(nativeImportColorLut)(JNIEnv* env, jclass, jlong handle, jlong layer, jint effect, jstring path) {
    NativeContext* c = ctx_of(handle);
    if (!c || !path) return static_cast<jint>(Errc::InvalidArgument);
    const char* p = env->GetStringUTFChars(path, nullptr);
    if (!p) return static_cast<jint>(Errc::OutOfMemory);
    const Status result = c->engine.import_color_lut(static_cast<u64>(layer), static_cast<u32>(effect), p);
    env->ReleaseStringUTFChars(path, p);
    return result.raw();
}
AUREA_JNI jstring AUREA_FN(nativeColorLutName)(JNIEnv* env, jclass, jlong handle, jlong layer, jint effect) {
    NativeContext* c = ctx_of(handle);
    const auto name = c ? c->engine.color_lut_name(layer, effect) : std::string{};
    return env->NewStringUTF(name.c_str());
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
    const u32 n = env->GetArrayLength(in) >= 20 ? 20 : 18;
    f32 v[20]{};
    env->GetFloatArrayRegion(in, 0, n, v);
    return c->engine.set_text_style(static_cast<u64>(layer), v, n) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeQueryTextStyle)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out || env->GetArrayLength(out) < 18) return JNI_FALSE;
    const u32 n = env->GetArrayLength(out) >= 20 ? 20 : 18;
    f32 v[20]{};
    if (!c->engine.query_text_style(static_cast<u64>(layer), v, n)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, n, v);
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

// =============================================================================
// Substituir mídia, informações da mídia e arquivo do projeto (EngineMedia.cpp,
// ProjectPackage.cpp).
// =============================================================================
AUREA_JNI jlong AUREA_FN(nativeReplaceLayerVideo)(JNIEnv* env, jclass, jlong handle, jlong layer, jstring source, jstring name) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    VideoImport request;
    request.sourcePath = to_string(env, source);
    request.displayName = to_string(env, name);
    const Result<u64> r = c->engine.replace_layer_video(static_cast<u64>(layer), request);
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jlong AUREA_FN(nativeReplaceLayerImage)(JNIEnv* env, jclass, jlong handle, jlong layer, jobject rgba, jint width,
                                                  jint height, jstring name, jstring source) {
    NativeContext* c = ctx_of(handle);
    if (!c || width <= 0 || height <= 0) return -static_cast<jlong>(Errc::InvalidArgument);
    const auto* pixels = pod_buffer<const u8>(env, rgba, static_cast<jlong>(width) * height * 4);
    if (!pixels) return -static_cast<jlong>(Errc::InvalidArgument);
    const std::string n = to_string(env, name);
    const std::string src = to_string(env, source);
    const Result<u64> r = c->engine.replace_layer_image(static_cast<u64>(layer), pixels, static_cast<u32>(width),
                                                        static_cast<u32>(height), n.c_str(), src.c_str());
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

AUREA_JNI jstring AUREA_FN(nativeLayerSourcePath)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    const std::string p = c->engine.layer_source_path(static_cast<u64>(layer));
    return p.empty() ? nullptr : env->NewStringUTF(p.c_str());
}

namespace {
jobjectArray string_array(JNIEnv* env, const std::vector<std::string>& items) {
    jclass cls = env->FindClass("java/lang/String");
    jobjectArray out = env->NewObjectArray(static_cast<jsize>(items.size()), cls, nullptr);
    if (!out) return nullptr;
    for (jsize i = 0; i < static_cast<jsize>(items.size()); ++i) {
        jstring s = env->NewStringUTF(items[static_cast<usize>(i)].c_str());
        env->SetObjectArrayElement(out, i, s);
        env->DeleteLocalRef(s);
    }
    return out;
}
} // namespace

/// Mídias de um `.aurea` fechado: 4 strings por mídia (gravado, legível, nome,
/// tipo). null = o projeto não abre.
AUREA_JNI jobjectArray AUREA_FN(nativeProjectFileMedia)(JNIEnv* env, jclass, jlong handle, jstring path) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    std::vector<package::MediaRef> refs;
    if (!c->engine.project_file_media(to_string(env, path).c_str(), refs).ok()) return nullptr;
    std::vector<std::string> flat;
    for (const auto& m : refs) {
        flat.push_back(m.stored);
        flat.push_back(m.resolved);
        flat.push_back(m.name);
        flat.push_back(std::to_string(static_cast<int>(m.kind)));
    }
    return string_array(env, flat);
}

/// `media`: 3 strings por mídia (gravado, legível agora, nome). Devolve
/// [código do erro (0 = ok), incluídas, puladas].
AUREA_JNI jintArray AUREA_FN(nativeExportProjectPackage)(JNIEnv* env, jclass, jstring project, jstring out, jstring title,
                                                         jstring appVersion, jobjectArray media) {
    std::vector<package::MediaFile> files;
    const jsize n = media ? env->GetArrayLength(media) : 0;
    for (jsize i = 0; i + 2 < n; i += 3) {
        package::MediaFile f;
        auto get = [&](jsize k) {
            auto s = static_cast<jstring>(env->GetObjectArrayElement(media, k));
            std::string v = to_string(env, s);
            if (s) env->DeleteLocalRef(s);
            return v;
        };
        f.stored = get(i);
        f.readable = get(i + 1);
        f.name = get(i + 2);
        files.push_back(std::move(f));
    }
    package::ExportResult r;
    const Status s = package::write_package(to_string(env, project), to_string(env, out), to_string(env, title),
                                            to_string(env, appVersion), files, &r);
    const jint vals[3] = {static_cast<jint>(s.code()), static_cast<jint>(r.included), static_cast<jint>(r.skipped)};
    jintArray a = env->NewIntArray(3);
    if (a) env->SetIntArrayRegion(a, 0, 3, vals);
    return a;
}

/// Devolve [código (0 = ok), título, versão do app, religadas, ausentes].
AUREA_JNI jobjectArray AUREA_FN(nativeImportProjectPackage)(JNIEnv* env, jclass, jstring packageFile, jstring projectOut,
                                                            jstring mediaDir) {
    package::ImportResult r;
    const Status s = package::read_package(to_string(env, packageFile), to_string(env, projectOut), to_string(env, mediaDir), r);
    return string_array(env, {std::to_string(static_cast<int>(s.code())), r.title, r.appVersion,
                              std::to_string(r.relinked), std::to_string(r.missing)});
}

AUREA_JNI jobjectArray AUREA_FN(nativeExtractModelArchive)(JNIEnv* env, jclass, jstring archive, jstring directory) {
    std::vector<std::string> files;
    if (!package::extract_model_archive(to_string(env, archive), to_string(env, directory), files).ok()) return nullptr;
    return string_array(env, files);
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

namespace {
std::string utf8_of(JNIEnv* env, jbyteArray a);
jbyteArray bytes_of(JNIEnv* env, const std::string& s);
}

AUREA_JNI jbyteArray AUREA_FN(nativeTranscribeLocal)(JNIEnv* env, jclass, jlong handle, jlong layer, jbyteArray model, jbyteArray language, jboolean translateEnglish) {
    auto* c = ctx_of(handle); if (!c) return nullptr;
    auto result = c->engine.transcribe_local(static_cast<u64>(layer), utf8_of(env, model), utf8_of(env, language), translateEnglish == JNI_TRUE);
    if (!result) { env->ThrowNew(env->FindClass("java/io/IOException"), std::string(result.status().detail()).c_str()); return nullptr; }
    std::string output;
    for (const auto& word : *result) output += std::to_string(word.start) + "\t" + std::to_string(word.end) + "\t" + word.text + "\n";
    return bytes_of(env, output);
}
AUREA_JNI jint AUREA_FN(nativeCaptionProgress)(JNIEnv*, jclass, jlong handle, jboolean cancel) {
    auto* c = ctx_of(handle); if (!c) return 0;
    if (cancel) c->engine.captionCancelled.store(true);
    return c->engine.captionProgress.load();
}
AUREA_JNI jint AUREA_FN(nativeCaptionCount)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.caption_count(static_cast<u64>(layer))) : 0;
}

AUREA_JNI jbyteArray AUREA_FN(nativeCaptionTracks)(JNIEnv* env, jclass, jlong handle) {
    auto* c = ctx_of(handle); return bytes_of(env, c ? c->engine.caption_tracks() : "[]");
}
AUREA_JNI jbyteArray AUREA_FN(nativeSaveCaptionBundle)(JNIEnv* env, jclass, jlong handle, jlong layer, jbyteArray name) {
    auto* c = ctx_of(handle); return bytes_of(env, c ? c->engine.save_caption_bundle(static_cast<u64>(layer), utf8_of(env,name)) : "");
}
AUREA_JNI jboolean AUREA_FN(nativeApplyCaptionBundle)(JNIEnv* env, jclass, jlong handle, jlong layer, jbyteArray data) {
    auto* c = ctx_of(handle); return c && c->engine.apply_caption_bundle(static_cast<u64>(layer), utf8_of(env,data));
}
AUREA_JNI jboolean AUREA_FN(nativeEditCaptionTrack)(JNIEnv* env, jclass, jlong handle, jlong layer, jbyteArray command) {
    auto* c = ctx_of(handle); return c && c->engine.edit_caption_track(static_cast<u64>(layer), utf8_of(env, command));
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

AUREA_JNI jint AUREA_FN(nativeDuplicateTextAnimator)(JNIEnv*, jclass, jlong handle, jlong layer, jint index) {
    NativeContext* c = ctx_of(handle);
    return c && index >= 0 ? c->engine.duplicate_text_animator(static_cast<u64>(layer), static_cast<u32>(index)) : -1;
}

AUREA_JNI jboolean AUREA_FN(nativeMoveTextAnimator)(JNIEnv*, jclass, jlong handle, jlong layer, jint from, jint to) {
    NativeContext* c = ctx_of(handle);
    return c && from >= 0 && to >= 0 && c->engine.move_text_animator(static_cast<u64>(layer), static_cast<u32>(from), static_cast<u32>(to)) ? JNI_TRUE : JNI_FALSE;
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

// --- Animadores de camada, desfoque por camada e escopo de grupo --------------

/// Animação de texto 3D: 3 modos × Engine::kText3DAnimFloats (nulo = não é texto 3D).
AUREA_JNI jfloatArray AUREA_FN(nativeQueryText3dAnim)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    f32 v[3 * Engine::kText3DAnimFloats] = {};
    if (!c || !c->engine.query_text3d_anim(static_cast<u64>(layer), v)) return nullptr;
    jfloatArray out = env->NewFloatArray(static_cast<jsize>(3 * Engine::kText3DAnimFloats));
    if (out) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(3 * Engine::kText3DAnimFloats), v);
    return out;
}

AUREA_JNI jboolean AUREA_FN(nativeApplyText3dAnim)(JNIEnv*, jclass, jlong handle, jlong layer, jint preset, jint mode, jint unit,
                                                   jfloat durationSec, jfloat staggerMs) {
    NativeContext* c = ctx_of(handle);
    return c && mode >= 0 && unit >= 0
                   && c->engine.apply_text3d_anim(static_cast<u64>(layer), preset, static_cast<u32>(mode), static_cast<u32>(unit),
                                                  durationSec, staggerMs)
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jfloatArray AUREA_FN(nativeQueryLayerAnimators)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    std::vector<f32> v(32 * Engine::kLayerAnimFloats);
    const u32 n = c->engine.query_layer_animators(static_cast<u64>(layer), v.data(), static_cast<u32>(v.size()));
    jfloatArray out = env->NewFloatArray(static_cast<jsize>(n * Engine::kLayerAnimFloats));
    if (out && n) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(n * Engine::kLayerAnimFloats), v.data());
    return out;
}

AUREA_JNI jint AUREA_FN(nativeAddLayerAnimator)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? c->engine.add_layer_animator(static_cast<u64>(layer)) : -1;
}

AUREA_JNI jboolean AUREA_FN(nativeRemoveLayerAnimator)(JNIEnv*, jclass, jlong handle, jlong layer, jint index) {
    NativeContext* c = ctx_of(handle);
    return c && index >= 0 && c->engine.remove_layer_animator(static_cast<u64>(layer), static_cast<u32>(index)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetLayerAnimator)(JNIEnv* env, jclass, jlong handle, jlong layer, jint index, jfloatArray in) {
    NativeContext* c = ctx_of(handle);
    if (!c || !in || index < 0 || env->GetArrayLength(in) < static_cast<jsize>(Engine::kLayerAnimFloats)) return JNI_FALSE;
    f32 v[Engine::kLayerAnimFloats];
    env->GetFloatArrayRegion(in, 0, Engine::kLayerAnimFloats, v);
    return c->engine.set_layer_animator(static_cast<u64>(layer), static_cast<u32>(index), v) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetLayerAnimParam)(JNIEnv*, jclass, jlong handle, jlong layer, jint index, jint param, jfloat value) {
    NativeContext* c = ctx_of(handle);
    return c && index >= 0 && param >= 0
                   && c->engine.set_layer_anim_param(static_cast<u64>(layer), static_cast<u32>(index), static_cast<u32>(param), value)
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeToggleLayerAnimKey)(JNIEnv*, jclass, jlong handle, jlong layer, jint index, jint param) {
    NativeContext* c = ctx_of(handle);
    return c && index >= 0 && param >= 0
                   && c->engine.toggle_layer_anim_key(static_cast<u64>(layer), static_cast<u32>(index), static_cast<u32>(param))
               ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeCopyLayerAnimators)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.copy_layer_animators(static_cast<u64>(layer))) : 0;
}

AUREA_JNI jint AUREA_FN(nativePasteLayerAnimators)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    NativeContext* c = ctx_of(handle);
    if (!c) return 0;
    const auto v = jlongs(env, ids);
    return static_cast<jint>(c->engine.paste_layer_animators(v.data(), static_cast<u32>(v.size())));
}

AUREA_JNI jint AUREA_FN(nativeLayerAnimatorClipboard)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.layer_animator_clipboard()) : 0;
}

// --- Roto Brush (EngineRoto.cpp) ---------------------------------------------
AUREA_JNI jboolean AUREA_FN(nativeRotoAddStroke)(JNIEnv* env, jclass, jlong handle, jlong layer, jint effectId,
                                                 jboolean background, jfloat radius, jfloatArray xy) {
    NativeContext* c = ctx_of(handle);
    if (!c || !xy) return JNI_FALSE;
    const jsize n = env->GetArrayLength(xy);
    std::vector<f32> pts(static_cast<usize>(n));
    if (n > 0) env->GetFloatArrayRegion(xy, 0, n, pts.data());
    return c->engine.roto_add_stroke(static_cast<u64>(layer), static_cast<u32>(effectId), background == JNI_TRUE, radius,
                                     pts.data(), static_cast<u32>(n / 2)) ? JNI_TRUE : JNI_FALSE;
}
AUREA_JNI jboolean AUREA_FN(nativeRotoUndoStroke)(JNIEnv*, jclass, jlong handle, jlong layer, jint effectId) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.roto_undo_stroke(static_cast<u64>(layer), static_cast<u32>(effectId)) ? JNI_TRUE : JNI_FALSE;
}
AUREA_JNI jboolean AUREA_FN(nativeRotoPropagate)(JNIEnv*, jclass, jlong handle, jlong layer, jint effectId) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.roto_propagate(static_cast<u64>(layer), static_cast<u32>(effectId)) ? JNI_TRUE : JNI_FALSE;
}
AUREA_JNI void AUREA_FN(nativeRotoCancel)(JNIEnv*, jclass, jlong handle) {
    if (NativeContext* c = ctx_of(handle)) c->engine.roto_cancel();
}
AUREA_JNI jboolean AUREA_FN(nativeRotoSetView)(JNIEnv*, jclass, jlong handle, jlong layer, jint effectId, jint mode) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.roto_set_view(static_cast<u64>(layer), static_cast<u32>(effectId), static_cast<u32>(std::max(0, mode))) ? JNI_TRUE : JNI_FALSE;
}
/// [feitos, total, rodando, falhou, traços aqui, traços total]
AUREA_JNI jlongArray AUREA_FN(nativeRotoStatus)(JNIEnv* env, jclass, jlong handle, jlong layer, jint effectId) {
    NativeContext* c = ctx_of(handle);
    jlong v[6]{};
    if (c) {
        i64 p[4]{}; u32 info[3]{};
        (void)c->engine.roto_progress(p);
        (void)c->engine.roto_stroke_info(static_cast<u64>(layer), static_cast<u32>(effectId), info);
        v[0] = p[0]; v[1] = p[1]; v[2] = p[2]; v[3] = p[3]; v[4] = info[0]; v[5] = info[1];
    }
    jlongArray out = env->NewLongArray(6);
    if (out) env->SetLongArrayRegion(out, 0, 6, v);
    return out;
}

AUREA_JNI jboolean AUREA_FN(nativeSetLayerMotionBlurLength)(JNIEnv*, jclass, jlong handle, jlong layer, jfloat factor) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_layer_motion_blur_length(static_cast<u64>(layer), factor) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jfloat AUREA_FN(nativeQueryLayerMotionBlurLength)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? c->engine.query_layer_motion_blur_length(static_cast<u64>(layer)) : 1.0f;
}

AUREA_JNI jboolean AUREA_FN(nativeSetAdjustmentScope)(JNIEnv*, jclass, jlong handle, jlong layer, jint scope) {
    NativeContext* c = ctx_of(handle);
    return c && scope >= 0 && c->engine.set_adjustment_scope(static_cast<u64>(layer), static_cast<u32>(scope)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeQueryAdjustmentScope)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.query_adjustment_scope(static_cast<u64>(layer))) : 0;
}

AUREA_JNI jboolean AUREA_FN(nativeSetAdjustmentTarget)(JNIEnv*, jclass, jlong handle, jlong layer, jlong target, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_adjustment_target(static_cast<u64>(layer), static_cast<u64>(target), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jlongArray AUREA_FN(nativeQueryAdjustmentTargets)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    std::vector<u64> ids;
    if (c) {
        ids.resize(c->engine.query_adjustment_targets(static_cast<u64>(layer), nullptr, 0));
        if (!ids.empty()) ids.resize(c->engine.query_adjustment_targets(static_cast<u64>(layer), ids.data(), static_cast<u32>(ids.size())));
    }
    jlongArray arr = env->NewLongArray(static_cast<jsize>(ids.size()));
    if (arr && !ids.empty()) {
        std::vector<jlong> v(ids.begin(), ids.end());
        env->SetLongArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    }
    return arr;
}

/// Presets do tipo de efeito (Effect::presets): pares (id estável, nome do motor).
AUREA_JNI jobjectArray AUREA_FN(nativeEffectPresets)(JNIEnv* env, jclass, jlong handle, jint typeId) {
    NativeContext* c = ctx_of(handle);
    std::vector<std::string> flat;
    if (c && typeId >= 0) {
        for (auto& [id, name] : c->engine.effect_presets(static_cast<u32>(typeId))) {
            flat.push_back(id);
            flat.push_back(name);
        }
    }
    return string_array(env, flat);
}

AUREA_JNI jboolean AUREA_FN(nativeApplyEffectPreset)(JNIEnv*, jclass, jlong handle, jlong layer, jint effectId, jint preset) {
    NativeContext* c = ctx_of(handle);
    return c && effectId >= 0 && preset >= 0
        && c->engine.apply_effect_preset(static_cast<u64>(layer), static_cast<u32>(effectId), static_cast<u32>(preset)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetGroupCameraPassThrough)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_group_camera_pass_through(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeQueryGroupCameraPassThrough)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.query_group_camera_pass_through(static_cast<u64>(layer))) : -1;
}

AUREA_JNI jboolean AUREA_FN(nativeSetLayerAcceptsLights)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_layer_accepts_lights(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeEnableLayer3D)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.enable_layer_3d(static_cast<u64>(layer)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeQueryLayerAcceptsLights)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.query_layer_accepts_lights(static_cast<u64>(layer))) : -1;
}

AUREA_JNI jstring AUREA_FN(nativeAddLayersToGroup)(JNIEnv* env, jclass, jlong handle, jlongArray ids, jlong group) {
    NativeContext* c = ctx_of(handle);
    if (!c) return env->NewStringUTF("motor indisponivel");
    const auto v = jlongs(env, ids);
    std::string why;
    const Result<u32> r = c->engine.add_layers_to_group(v.data(), static_cast<u32>(v.size()), static_cast<u64>(group), &why);
    if (r.ok()) return nullptr;
    return env->NewStringUTF(why.empty() ? "nao deu para agrupar" : why.c_str());
}

AUREA_JNI jstring AUREA_FN(nativeRemoveLayerFromGroup)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    if (!c) return env->NewStringUTF("motor indisponivel");
    std::string why;
    const Result<u64> r = c->engine.remove_layer_from_group(static_cast<u64>(layer), &why);
    if (r.ok()) return nullptr;
    return env->NewStringUTF(why.empty() ? "nao deu para tirar do grupo" : why.c_str());
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

/// Preset de efeitos com SÓ o efeito `effectId` (id estável da instância) e
/// os keyframes dele. Nulo = camada ou efeito não existe.
AUREA_JNI jbyteArray AUREA_FN(nativeSaveEffectPreset)(JNIEnv* env, jclass, jlong handle, jlong layer, jint effectId, jbyteArray name) {
    NativeContext* c = ctx_of(handle);
    if (!c || effectId < 0) return nullptr;
    const std::string js = c->engine.save_effect_preset(static_cast<u64>(layer), static_cast<u32>(effectId), utf8_of(env, name));
    return js.empty() ? nullptr : bytes_of(env, js);
}

/// XML do Alight Motion (ou o pacote .zip/.amproj, em bytes) → envelope JSON
/// {"preset", "name", "layer", "mapped", "skipped", "warnings", "error"}.
/// "preset" vazio = nada aproveitável ("error" diz o porquê). Não mexe no projeto.
AUREA_JNI jbyteArray AUREA_FN(nativeImportAlightMotion)(JNIEnv* env, jclass, jlong handle, jbyteArray data) {
    NativeContext* c = ctx_of(handle);
    presets::AlightImportReport report;
    std::string js;
    if (c) js = c->engine.import_alight_motion(utf8_of(env, data), report);
    else report.error = "motor indisponivel";
    return bytes_of(env, presets::alight_import_envelope(js, report));
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

AUREA_JNI jbyteArray AUREA_FN(nativeMakeCurvePreset)(JNIEnv* env, jclass, jbyteArray name, jint interp, jfloat x1, jfloat y1, jfloat x2, jfloat y2, jint power) {
    const jint i = std::clamp(interp, 0, static_cast<jint>(kLastInterpolation));
    // Curva com parâmetros: o marcador em y2 atravessa (Math.hpp, kEaseParamMarker).
    const bool params = ease_has_params(static_cast<Interpolation>(i)) && y2 == kEaseParamMarker;
    return bytes_of(env, presets::make_curve_preset(utf8_of(env, name), static_cast<Interpolation>(i), std::clamp(x1, 0.0f, 1.0f),
                                                    std::clamp(y1, -2.0f, 3.0f), std::clamp(x2, 0.0f, 1.0f),
                                                    params ? kEaseParamMarker : std::clamp(y2, -2.0f, 3.0f),
                                                    static_cast<u32>(std::clamp(power, 1, 3))));
}

/// O trecho de curva amostrado pelo MOTOR (`sample_keyframe_ease`) para o
/// gráfico do editor de curva: `out` recebe size valores em t = i/(size−1).
/// Devolve quantos escreveu (0 = pedido inválido).
AUREA_JNI jint AUREA_FN(nativeSampleEase)(JNIEnv* env, jclass, jint interp, jfloat x1, jfloat y1, jfloat x2, jfloat y2,
                                          jint power, jfloatArray out) {
    if (!out || interp < 0 || interp > static_cast<jint>(kLastInterpolation)) return 0;
    const jsize n = std::min<jsize>(env->GetArrayLength(out), 1025);
    if (n < 2) return 0;
    Keyframe k;
    k.interp = static_cast<Interpolation>(interp);
    k.bx1 = x1; k.by1 = y1; k.bx2 = x2; k.by2 = y2;
    k.easePower = clamp_ease_power(static_cast<u32>(std::max(power, 0)));
    f32 values[1025];
    const u32 count = sample_keyframe_ease(k, values, static_cast<u32>(n));
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(count), values);
    return env->ExceptionCheck() ? 0 : static_cast<jint>(count);
}

/// Lê um preset de curva: [interp, x1, y1, x2, y2, força]. Nulo = inválido.
AUREA_JNI jfloatArray AUREA_FN(nativeParseCurvePreset)(JNIEnv* env, jclass, jbyteArray json) {
    presets::Preset p;
    if (!presets::parse(utf8_of(env, json), p) || p.kind != presets::PresetKind::Curve) return nullptr;
    const f32 v[6] = {static_cast<f32>(p.curveInterp), p.x1, p.y1, p.x2, p.y2, static_cast<f32>(p.curvePower)};
    jfloatArray out = env->NewFloatArray(6);
    if (out) env->SetFloatArrayRegion(out, 0, 6, v);
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

AUREA_JNI jboolean AUREA_FN(nativeStartMotionTrack)(JNIEnv* env, jclass, jlong handle, jlong layer, jint tool, jint model, jboolean backward, jfloatArray points, jfloat feature, jfloat search) {
    auto* c=ctx_of(handle);if(!c||!points)return JNI_FALSE;
    const auto n=env->GetArrayLength(points);if(n>8||n%2)return JNI_FALSE;
    f32 xy[8]{};if(n)env->GetFloatArrayRegion(points,0,n,xy);
    return c->engine.start_motion_track(layer,tool,model,backward,xy,n/2,feature,search)?JNI_TRUE:JNI_FALSE;
}
AUREA_JNI void AUREA_FN(nativeCancelMotionTrack)(JNIEnv*,jclass,jlong handle){if(auto* c=ctx_of(handle))c->engine.cancel_motion_track();}
AUREA_JNI jboolean AUREA_FN(nativeRestoreMotionTrack)(JNIEnv*,jclass,jlong handle,jlong layer){auto* c=ctx_of(handle);return c&&c->engine.restore_motion_track(layer)?JNI_TRUE:JNI_FALSE;}
AUREA_JNI jstring AUREA_FN(nativeMotionTrackStatus)(JNIEnv* env,jclass,jlong handle,jfloatArray out){
    auto* c=ctx_of(handle);if(!c||!out||env->GetArrayLength(out)<12)return nullptr;
    auto s=c->engine.motion_track_status();const f32 v[]={static_cast<f32>(s.state),s.progress,static_cast<f32>(s.frames),static_cast<f32>(s.validFrames),static_cast<f32>(s.tool),static_cast<f32>(s.lost),static_cast<f32>(s.reacquired),s.confidence,s.errorPx,s.cropPercent,static_cast<f32>(s.memoryBytes)/1048576.f,0};
    env->SetFloatArrayRegion(out,0,12,v);return env->NewStringUTF(s.message.c_str());
}
AUREA_JNI jlong AUREA_FN(nativeApplyMotionTrack)(JNIEnv*,jclass,jlong handle,jlong target,jint apply,jboolean lock,jfloat smooth,jfloat maxScale,jint crop){
    auto* c=ctx_of(handle);if(!c)return -1;auto r=c->engine.apply_motion_track(target,apply,lock,smooth,maxScale,crop);return r.ok()?static_cast<jlong>(*r):-static_cast<jlong>(r.code());
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

AUREA_JNI jlong AUREA_FN(nativeApplyCameraTrack)(JNIEnv*, jclass, jlong handle, jlong frame, jfloat x0, jfloat y0, jfloat x1, jfloat y1) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> r = c->engine.apply_camera_track(frame, Vec4{x0, y0, x1, y1});
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}
AUREA_JNI jboolean AUREA_FN(nativeRefineCameraTrack)(JNIEnv*,jclass,jlong handle,jboolean remove,jint motion,jfloat fov) {
    auto* c=ctx_of(handle);return c&&c->engine.refine_camera_track(remove,motion,fov)?JNI_TRUE:JNI_FALSE;
}
AUREA_JNI jint AUREA_FN(nativeCameraTrackTarget)(JNIEnv* env,jclass,jlong handle,jlong frame,jfloatArray out){
    auto* c=ctx_of(handle);if(!c||!out||env->GetArrayLength(out)<64)return 0;f32 data[64]{};const auto n=c->engine.camera_track_target(frame,data,32);if(n)env->SetFloatArrayRegion(out,0,n*2,data);return static_cast<jint>(n);
}
AUREA_JNI jboolean AUREA_FN(nativeCalibrateCameraScene)(JNIEnv*,jclass,jlong handle,jint operation,jfloat distance){auto* c=ctx_of(handle);return c&&c->engine.calibrate_camera_scene(operation,distance)?JNI_TRUE:JNI_FALSE;}
AUREA_JNI jboolean AUREA_FN(nativePlaceModelOnTrack)(JNIEnv*,jclass,jlong handle,jlong layer){auto* c=ctx_of(handle);return c&&c->engine.place_model_on_track(layer)?JNI_TRUE:JNI_FALSE;}

AUREA_JNI jboolean AUREA_FN(nativeRestoreCameraTrack)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.restore_camera_track(static_cast<u64>(layer)) ? JNI_TRUE : JNI_FALSE;
}
AUREA_JNI jint AUREA_FN(nativeCameraTrackDetails)(JNIEnv* env, jclass, jlong handle, jlong frame, jfloatArray out) {
    NativeContext* c = ctx_of(handle); if (!c || !out) return 0;
    const u32 cap = std::min<u32>(1500, static_cast<u32>(env->GetArrayLength(out) / 6));
    std::vector<f32> data(cap * 6);
    const u32 count = c->engine.camera_track_features_detail(frame, data.data(), cap);
    if (count) env->SetFloatArrayRegion(out, 0, count * 6, data.data());
    return static_cast<jint>(count);
}
AUREA_JNI jint AUREA_FN(nativeSelectCameraTrackPoints)(JNIEnv* env, jclass, jlong handle, jintArray ids, jint operation) {
    NativeContext* c = ctx_of(handle); if (!c || !ids || env->GetArrayLength(ids) > 1500) return 0;
    std::vector<jint> input(static_cast<usize>(env->GetArrayLength(ids)));
    if (!input.empty()) env->GetIntArrayRegion(ids, 0, static_cast<jsize>(input.size()), input.data());
    std::vector<u32> values; for (jint value : input) if (value >= 0) values.push_back(static_cast<u32>(value));
    return static_cast<jint>(c->engine.select_camera_track_points(values.data(), static_cast<u32>(values.size()), static_cast<u32>(operation)));
}
AUREA_JNI jlong AUREA_FN(nativeCreateCameraTrackObject)(JNIEnv*, jclass, jlong handle, jint kind) {
    NativeContext* c = ctx_of(handle); if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const auto result = c->engine.apply_camera_track(-1, {}, static_cast<u32>(kind));
    return result.ok() ? static_cast<jlong>(*result) : -static_cast<jlong>(result.code());
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

AUREA_JNI jboolean AUREA_FN(nativeSetTimeRemapValue)(JNIEnv*, jclass, jlong handle, jlong layer, jlong frame, jfloat value) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_time_remap_value(static_cast<u64>(layer), frame, value) ? JNI_TRUE : JNI_FALSE;
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

AUREA_JNI jboolean AUREA_FN(nativeReverseTimeRemap)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.reverse_time_remap(static_cast<u64>(layer)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetKeepPitch)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_keep_pitch(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
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

AUREA_JNI jboolean AUREA_FN(nativeQueryMotionBlurSettings)(JNIEnv* env, jclass, jlong handle, jfloatArray output) {
    NativeContext* c = ctx_of(handle);
    MotionBlurSettings settings;
    if (!c || !output || env->GetArrayLength(output) < 6 ||
        !c->engine.query_motion_blur_settings(settings)) return JNI_FALSE;
    const jfloat values[] = {settings.enabled ? 1.0f : 0.0f, settings.shutterAngle,
        settings.shutterPhase, static_cast<jfloat>(settings.samples),
        static_cast<jfloat>(settings.adaptiveLimit), static_cast<jfloat>(settings.previewSamples)};
    env->SetFloatArrayRegion(output, 0, 6, values);
    return env->ExceptionCheck() ? JNI_FALSE : JNI_TRUE;
}

AUREA_JNI jboolean AUREA_FN(nativeSetMotionBlurSettings)(JNIEnv*, jclass, jlong handle,
    jboolean enabled, jfloat angle, jfloat phase, jint samples, jint adaptiveLimit) {
    NativeContext* c = ctx_of(handle);
    if (!c || samples < 0 || adaptiveLimit < 0) return JNI_FALSE;
    return c->engine.set_motion_blur_settings(enabled == JNI_TRUE, angle, phase,
        static_cast<u32>(samples), static_cast<u32>(adaptiveLimit)) ? JNI_TRUE : JNI_FALSE;
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

AUREA_JNI jboolean AUREA_FN(nativeEditClipTime)(JNIEnv*, jclass, jlong handle, jlong layer, jint operation,
                                               jlong amount, jlong previous, jlong next) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.edit_clip_time(layer, static_cast<u32>(operation), amount, previous, next) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jint AUREA_FN(nativeQueryClipTimeActions)(JNIEnv*, jclass, jlong handle, jlong layer, jlong frame) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.query_clip_time_actions(layer, frame)) : 0;
}

AUREA_JNI jboolean AUREA_FN(nativeSetLayerMagneticTrack)(JNIEnv*, jclass, jlong handle, jlong layer, jboolean on) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.set_layer_magnetic_track(static_cast<u64>(layer), on == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeLayerMagneticTrack)(JNIEnv*, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.layer_magnetic_track(static_cast<u64>(layer)) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeReorderClip)(JNIEnv*, jclass, jlong handle, jlong layer, jlong targetFrame) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.reorder_clip(static_cast<u64>(layer), targetFrame) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeMoveLayerToRow)(JNIEnv*, jclass, jlong handle, jlong layer, jlong anchor, jint mode) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.move_layer_to_row(static_cast<u64>(layer), static_cast<u64>(anchor), mode) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeTrimComposition)(JNIEnv*, jclass, jlong handle, jlong frame) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.trim_composition(frame) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jlong AUREA_FN(nativeMarkBeatLive)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jlong>(c->engine.mark_beat_live()) : -1;
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

AUREA_JNI jboolean AUREA_FN(nativeEditMarker)(JNIEnv* env, jclass, jlong handle, jlong from, jlong to, jint color, jbyteArray label) {
    NativeContext* c = ctx_of(handle);
    if (!c || !label) return JNI_FALSE;
    const jsize size = env->GetArrayLength(label);
    if (size > 1024) return JNI_FALSE;
    std::string text(static_cast<usize>(size), '\0');
    if (size) env->GetByteArrayRegion(label, 0, size, reinterpret_cast<jbyte*>(text.data()));
    return c->engine.edit_marker(from, to, static_cast<u32>(color), text) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jboolean AUREA_FN(nativeDeleteMarker)(JNIEnv*, jclass, jlong handle, jlong frame) {
    NativeContext* c = ctx_of(handle);
    return c && c->engine.delete_marker(frame) ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jbyteArray AUREA_FN(nativeMarkerLabel)(JNIEnv* env, jclass, jlong handle, jlong frame) {
    NativeContext* c = ctx_of(handle);
    const std::string text = c ? c->engine.marker_label(frame) : std::string{};
    jbyteArray result = env->NewByteArray(static_cast<jsize>(text.size()));
    if (result && !text.empty()) env->SetByteArrayRegion(result, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte*>(text.data()));
    return result;
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

AUREA_JNI jstring AUREA_FN(nativePlaybackReport)(JNIEnv* env, jclass, jlong handle) {
    auto* c = ctx_of(handle);
    return env->NewStringUTF(c ? c->engine.playback_report().c_str() : "");
}
AUREA_JNI jlong AUREA_FN(nativeAudioPositionNs)(JNIEnv*, jclass, jlong handle) {
    auto* c = ctx_of(handle);
    return c ? c->engine.audio().position_ns() : 0;
}
AUREA_JNI jboolean AUREA_FN(nativeSetRawPlayback)(JNIEnv*, jclass, jlong handle, jboolean enabled) {
    auto* c = ctx_of(handle); return c && c->engine.set_raw_playback(enabled == JNI_TRUE);
}
AUREA_JNI void AUREA_FN(nativeSetSceneEditor)(JNIEnv*, jclass, jlong handle, jboolean enabled, jfloat yaw, jfloat pitch, jfloat distance) {
    if (auto* c = ctx_of(handle)) c->engine.set_scene_editor(enabled == JNI_TRUE, yaw, pitch, distance);
}
AUREA_JNI jlong AUREA_FN(nativeScenePick)(JNIEnv*, jclass, jlong handle, jfloat x, jfloat y, jfloat radius) {
    auto* c = ctx_of(handle);
    return c ? static_cast<jlong>(c->engine.scene_pick(x, y, radius)) : 0;
}
AUREA_JNI jint AUREA_FN(nativeSceneGuides)(JNIEnv* env, jclass, jlong handle, jfloatArray output) {
    auto* c = ctx_of(handle);
    if (!c || !output) return 0;
    f32 lines[256 * 5]{};
    const u32 capacity = std::min<u32>(256, static_cast<u32>(env->GetArrayLength(output)) / 5);
    const u32 count = c->engine.query_scene_guides(lines, capacity);
    if (count) env->SetFloatArrayRegion(output, 0, static_cast<jsize>(count * 5), lines);
    return static_cast<jint>(count);
}
AUREA_JNI jboolean AUREA_FN(nativeLayoutTransform)(JNIEnv*, jclass, jlong handle, jlong layer, jint property, jfloat value) {
    auto* c = ctx_of(handle);
    if (!c) return JNI_FALSE;
    Command command;
    command.type = CommandType::LayerLayoutTransform;
    command.shape_param = ShapeParamPayload{LayerId::unpack(static_cast<u64>(layer)), static_cast<u32>(property), value};
    const auto accepted = c->engine.submit_commands(&command, 1, nullptr, 0);
    c->engine.request_render();
    return accepted == 1 ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jlong AUREA_FN(nativeAddLight)(JNIEnv*, jclass, jlong handle, jint kind) {
    auto* c = ctx_of(handle); if (!c) return -1;
    const auto id = c->engine.add_light(static_cast<u32>(kind));
    return id.ok() ? static_cast<jlong>(*id) : -1;
}
AUREA_JNI jboolean AUREA_FN(nativeLightInfo)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray output) {
    auto* c = ctx_of(handle); f32 values[11]{};
    if (!c || !output || env->GetArrayLength(output) < 10) return JNI_FALSE;
    const auto count = std::min<jsize>(11, env->GetArrayLength(output));
    if (!c->engine.query_light(static_cast<u64>(layer), values, static_cast<u32>(count))) return JNI_FALSE;
    env->SetFloatArrayRegion(output, 0, count, values); return JNI_TRUE;
}
AUREA_JNI jboolean AUREA_FN(nativeSetLightParam)(JNIEnv*, jclass, jlong handle, jlong layer, jint param, jfloat value) {
    auto* c = ctx_of(handle); if (!c) return JNI_FALSE;
    Command command; command.type = CommandType::LayerSetLightParam;
    command.shape_param = ShapeParamPayload{LayerId::unpack(static_cast<u64>(layer)), static_cast<u32>(param), value};
    return c->engine.submit_commands(&command, 1, nullptr, 0) == 1 ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jlong AUREA_FN(nativeAddCamera)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> r = c->engine.add_camera();
    return r.ok() ? static_cast<jlong>(*r) : -static_cast<jlong>(r.status().code());
}

// --- Lente da câmera 3D (ver Engine::query_camera_lens) ----------------------
/// 9 valores: mm, FOV°, DOF, foco (px), f/, desfoque ×, px/m, máscara de
/// trilhas com keyframe, câmera ativa. Falso = não é câmera.
AUREA_JNI jboolean AUREA_FN(nativeQueryCameraLens)(JNIEnv* env, jclass, jlong handle, jlong layer, jfloatArray output) {
    auto* c = ctx_of(handle); f32 values[9]{};
    if (!c || !output || env->GetArrayLength(output) < 9 || !c->engine.query_camera_lens(static_cast<u64>(layer), values)) return JNI_FALSE;
    env->SetFloatArrayRegion(output, 0, 9, values); return JNI_TRUE;
}
/// Pick Focus: distância ao longo do eixo ótico até a superfície 3D sob o ponto
/// (px da composição). < 0 = nada 3D ali. Só mede; a UI grava com o comando.
AUREA_JNI jfloat AUREA_FN(nativePickFocusDistance)(JNIEnv*, jclass, jlong handle, jlong layer, jfloat compX, jfloat compY) {
    auto* c = ctx_of(handle); if (!c) return -1.f;
    return c->engine.pick_focus_distance(static_cast<u64>(layer), compX, compY);
}
/// LayerSetCameraParam: 0 mm, 1 DOF (0/1), 2 distância de foco, 3 f/, 4 desfoque ×.
AUREA_JNI jboolean AUREA_FN(nativeSetCameraParam)(JNIEnv*, jclass, jlong handle, jlong layer, jint param, jfloat value) {
    auto* c = ctx_of(handle); if (!c) return JNI_FALSE;
    Command command; command.type = CommandType::LayerSetCameraParam;
    command.shape_param = ShapeParamPayload{LayerId::unpack(static_cast<u64>(layer)), static_cast<u32>(param), value};
    const bool ok = c->engine.submit_commands(&command, 1, nullptr, 0) == 1;
    c->engine.request_render();
    return ok ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI jlong AUREA_FN(nativeAddNull)(JNIEnv*, jclass, jlong handle, jboolean threeD) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const Result<u64> r = c->engine.add_null(threeD == JNI_TRUE);
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

/// "Vincular a novo nulo": id do nulo criado (≥ 0) ou `-Errc` em caso de falha.
AUREA_JNI jlong AUREA_FN(nativeParentToNewNull)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const auto v = jlongs(env, ids);
    const Result<u64> r = c->engine.parent_to_new_null(v.data(), static_cast<u32>(v.size()));
    if (!r.ok()) return -static_cast<jlong>(r.status().code());
    return static_cast<jlong>(*r);
}

/// "Escalonar": quantas camadas andaram (≥ 0) ou `-Errc` em caso de recusa.
AUREA_JNI jint AUREA_FN(nativeStaggerLayers)(JNIEnv* env, jclass, jlong handle, jlongArray ids, jint stepFrames, jboolean keysOnly) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jint>(Errc::InvalidState);
    const auto v = jlongs(env, ids);
    const Result<u32> r = c->engine.stagger_layers(v.data(), static_cast<u32>(v.size()), static_cast<i64>(stepFrames), keysOnly == JNI_TRUE);
    if (!r.ok()) return -static_cast<jint>(r.status().code());
    return static_cast<jint>(*r);
}

AUREA_JNI jint AUREA_FN(nativeArrangeLayerTimes)(JNIEnv* env, jclass, jlong handle, jlongArray ids, jint mode, jlong playhead) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jint>(Errc::InvalidState);
    const auto v = jlongs(env, ids);
    const auto r = c->engine.arrange_layer_times(v.data(), static_cast<u32>(v.size()),
        static_cast<LayerTimeArrangement>(mode), static_cast<i64>(playhead));
    return r.ok() ? static_cast<jint>(*r) : -static_cast<jint>(r.status().code());
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
/// Memória medida pelo Kotlin AGORA (ActivityManager.MemoryInfo):
/// [totalMem, availMem, isLowRamDevice (0/1)]. Ausente = a da sondagem do início.
static scene3d::DeviceMemoryHint read_memory_hint(JNIEnv* env, jlongArray memory) {
    scene3d::DeviceMemoryHint h;
    if (!memory || env->GetArrayLength(memory) < 3) return h;
    jlong v[3] = {0, 0, 0};
    env->GetLongArrayRegion(memory, 0, 3, v);
    h.totalBytes = v[0] > 0 ? static_cast<u64>(v[0]) : 0;
    h.availableBytes = v[1] > 0 ? static_cast<u64>(v[1]) : 0;
    h.lowRam = v[2] != 0;
    return h;
}

AUREA_JNI jlong AUREA_FN(nativeImportModel)(JNIEnv* env, jclass, jlong handle, jstring path, jstring name,
                                            jobjectArray detailOut, jint quality, jlongArray memory) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    ModelImport req;
    req.path = to_string(env, path);
    req.displayName = to_string(env, name);
    req.quality = static_cast<scene3d::ModelQuality>(std::clamp<jint>(quality, 0, 2));
    req.memory = read_memory_hint(env, memory);
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

/// "Otimizar modelo": custo do arquivo e o que cabe neste aparelho, ANTES do
/// import. Layout (kModelPlanSlots), lido por AureaEngine.ModelPlan:
///   0 válido · 1 exato · 2 pesado · 3 pesado demais · 4 recomendada
///   5 triângulos · 6 vértices · 7 texturas · 8 maior lado de textura
///   9 orçamento (bytes) · 10..12 cabe (Original, Equilibrado, Leve)
///   13..15 pico estimado · 16..18 triângulos que ficam · 19..21 teto de textura
AUREA_JNI jlongArray AUREA_FN(nativeInspectModel)(JNIEnv* env, jclass, jlong handle, jstring path, jlongArray memory) {
    constexpr jsize kModelPlanSlots = 22;
    jlong v[kModelPlanSlots] = {};
    if (NativeContext* c = ctx_of(handle)) {
        const scene3d::ModelPlan plan = c->engine.inspect_model(to_string(env, path), read_memory_hint(env, memory));
        v[0] = plan.cost.valid;
        v[1] = plan.cost.exact;
        v[2] = plan.heavy;
        v[3] = plan.tooHeavy;
        v[4] = static_cast<jlong>(plan.recommended);
        v[5] = static_cast<jlong>(plan.cost.triangles);
        v[6] = static_cast<jlong>(plan.cost.vertices);
        v[7] = plan.cost.textures;
        v[8] = plan.cost.largestTextureSide;
        v[9] = static_cast<jlong>(plan.budget[0].memoryBytes);
        for (u32 q = 0; q < scene3d::kModelQualityCount; ++q) {
            v[10 + q] = plan.fits[q];
            v[13 + q] = static_cast<jlong>(plan.peakBytes[q]);
            v[16 + q] = static_cast<jlong>(plan.keptTriangles[q]);
            v[19 + q] = plan.budget[q].maxTextureSize;
        }
    }
    jlongArray out = env->NewLongArray(kModelPlanSlots);
    if (out) env->SetLongArrayRegion(out, 0, kModelPlanSlots, v);
    return out;
}

/// O último import de modelo: [triângulos do arquivo, que ficaram, texturas
/// reduzidas, texturas puladas, qualidade].
AUREA_JNI jlongArray AUREA_FN(nativeLastModelImport)(JNIEnv* env, jclass, jlong handle) {
    jlong v[5] = {};
    if (NativeContext* c = ctx_of(handle)) {
        const ModelImportReport r = c->engine.last_model_import();
        v[0] = r.sourceTriangles;
        v[1] = r.triangles;
        v[2] = r.texturesReduced;
        v[3] = r.texturesSkipped;
        v[4] = static_cast<jlong>(r.quality);
    }
    jlongArray out = env->NewLongArray(5);
    if (out) env->SetLongArrayRegion(out, 0, 5, v);
    return out;
}

/// Texturas/.mtl que o modelo da layer referencia e não achou, uma por linha.
AUREA_JNI jstring AUREA_FN(nativeModelMissingTextures)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    std::string joined;
    if (c) {
        for (const std::string& name : c->engine.model_missing_textures(static_cast<u64>(layer))) joined += name + "\n";
    }
    return env->NewStringUTF(joined.c_str());
}

/// Pasta do arquivo do modelo (com a barra no fim); vazio = não é modelo importado.
AUREA_JNI jstring AUREA_FN(nativeModelFolder)(JNIEnv* env, jclass, jlong handle, jlong layer) {
    NativeContext* c = ctx_of(handle);
    const std::string dir = c ? c->engine.model_folder(static_cast<u64>(layer)) : std::string{};
    return env->NewStringUTF(dir.c_str());
}

/// Relê o modelo depois das texturas copiadas: ≥ 0 = quantas ainda faltam; < 0 = −código.
AUREA_JNI jint AUREA_FN(nativeReloadModelTextures)(JNIEnv* env, jclass, jlong handle, jlong layer, jobjectArray detailOut) {
    NativeContext* c = ctx_of(handle);
    if (!c) return -static_cast<jint>(Errc::InvalidState);
    std::string detail;
    const Result<u32> r = c->engine.reload_model_textures(static_cast<u64>(layer), &detail);
    if (detailOut && env->GetArrayLength(detailOut) > 0) {
        jstring d = env->NewStringUTF(detail.c_str());
        env->SetObjectArrayElement(detailOut, 0, d);
        env->DeleteLocalRef(d);
    }
    if (!r.ok()) return -static_cast<jint>(r.status().code());
    return static_cast<jint>(*r);
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
                                              jdouble fps, jstring title, jfloatArray background) {
    NativeContext* c = ctx_of(handle);
    if (!c) return JNI_FALSE;
    const std::string t = to_string(env, title);
    // Fundo opcional (RGBA sRGB) da folha "Novo projeto"; nulo = preto.
    f32 bg[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const bool withBackground = background && env->GetArrayLength(background) >= 4;
    if (withBackground) env->GetFloatArrayRegion(background, 0, 4, bg);
    return c->engine.new_project(static_cast<u32>(width), static_cast<u32>(height), static_cast<f64>(fps),
                                 t.c_str(), withBackground ? bg : nullptr).ok() ? JNI_TRUE : JNI_FALSE;
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
    if (!path) return static_cast<jint>(c->engine.autosave_project().raw());
    const std::string p = to_string(env, path);
    return static_cast<jint>(c->engine.save_project(p.c_str()).raw());
}

/// Sair do app / segundo plano: grava se houver qualquer mudança, sem as regras
/// do autosave. -1 = já estava limpo (nada escrito); senão o código Errc.
AUREA_JNI jint AUREA_FN(nativeSaveProjectIfDirty)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    bool saved = false;
    const Status s = c->engine.save_project_if_dirty(&saved);
    if (s.ok() && !saved) return -1;
    return static_cast<jint>(s.raw());
}

/// Bits de Engine::LoadNotice da última abertura (0 = abriu limpo); nos 16
/// bits de cima, quantos assets faltaram. A UI avisa em vez de esconder.
AUREA_JNI jint AUREA_FN(nativeLoadNotice)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    if (!c) return 0;
    const u32 missing = std::min<u32>(c->engine.last_load_missing_assets(), 0x7FFFu);
    return static_cast<jint>((missing << 16) | (c->engine.last_load_notice() & 0xFFFFu));
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
/// composição; `codec` 0 = H.264, 1 = HEVC; `bitrateMbps` 0 = automático;
/// `quality` 0 Baixa / 1 Normal / 2 Alta; `rateMode` 0 CBR / 1 VBR;
/// `safeMode` 0..2 = o modo de segurança que o motor sugeriu depois de o
/// encoder travar (export/ExportWatchdog.hpp).
AUREA_JNI jint AUREA_FN(nativeStartExport)(JNIEnv* env, jclass, jlong handle, jstring outputPath, jint shortSide,
                                           jdouble fps, jint codec, jint bitrateMbps, jint aiUpscale, jboolean trimToContent,
                                           jint quality, jint rateMode, jint safeMode) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    const std::string p = to_string(env, outputPath);
    ExportSettings settings;
    settings.width = 0;
    settings.height = shortSide > 0 ? static_cast<u32>(shortSide) : 0;
    settings.fps = fps > 0.0 ? fps : 0.0;
    settings.videoCodec = codec == 1 ? ExportCodec::HEVC : ExportCodec::H264;
    settings.videoBitrateMbps = bitrateMbps > 0 ? static_cast<u32>(bitrateMbps) : 0;
    settings.aiUpscale = static_cast<u32>(aiUpscale);
    settings.trimToContent = trimToContent == JNI_TRUE;
    settings.quality = static_cast<u32>(std::clamp<jint>(quality, 0, 2));
    settings.rateMode = rateMode == 0 ? 0u : 1u;
    settings.safeMode = static_cast<u32>(std::clamp<jint>(safeMode, 0, static_cast<jint>(kExportSafeModeMax)));
    settings.audioBitrateKbps = kExportAudioKbps;
    return static_cast<jint>(c->engine.start_export(settings, p.c_str()).raw());
}

/// A taxa de vídeo (bps) que o export usaria — a mesma regra do motor, para a
/// tela mostrar o tamanho estimado sem conta própria.
AUREA_JNI jlong AUREA_FN(nativeExportBitrateBps)(JNIEnv*, jclass, jint width, jint height, jdouble fps, jint codec,
                                                jint quality, jint customMbps) {
    return static_cast<jlong>(export_video_bitrate_bps(static_cast<u32>(std::max(0, width)), static_cast<u32>(std::max(0, height)), fps,
        codec == 1 ? ExportCodec::HEVC : ExportCodec::H264, static_cast<ExportQuality>(std::clamp<jint>(quality, 0, 2)),
        static_cast<u32>(std::max(0, customMbps))));
}

AUREA_JNI jlong AUREA_FN(nativeExportDuration)(JNIEnv*, jclass, jlong handle, jboolean trimToContent) {
    NativeContext* c = ctx_of(handle);
    return c ? c->engine.query_export_duration(trimToContent == JNI_TRUE) : 0;
}

/// Export como imagem (export/ImageEncode.hpp). `format` 0 PNG do playhead,
/// 1 sequência PNG (.zip), 2 GIF; `shortSide` 0 = resolução da composição;
/// `maxWidth` largura máxima do GIF; `fps` 0 = padrão do formato.
namespace {
ImageExportSettings image_settings(jint format, jint shortSide, jint maxWidth, jdouble fps, jboolean trimToContent) {
    ImageExportSettings s;
    s.format = static_cast<ImageExportFormat>(std::clamp<jint>(format, 0, 2));
    s.shortSide = shortSide > 0 ? static_cast<u32>(shortSide) : 0u;
    s.maxWidth = maxWidth > 0 ? static_cast<u32>(maxWidth) : 0u;
    s.fps = fps > 0.0 ? fps : 0.0;
    s.trimToContent = trimToContent == JNI_TRUE;
    return s;
}
} // namespace

AUREA_JNI jint AUREA_FN(nativeStartImageExport)(JNIEnv* env, jclass, jlong handle, jstring outputPath, jint format,
                                                jint shortSide, jint maxWidth, jdouble fps, jboolean trimToContent) {
    NativeContext* c = ctx_of(handle);
    if (!c) return static_cast<jint>(Errc::InvalidState);
    const std::string p = to_string(env, outputPath);
    return static_cast<jint>(c->engine.start_image_export(image_settings(format, shortSide, maxWidth, fps, trimToContent),
                                                          p.c_str()).raw());
}

/// O plano do export como imagem: [largura, altura, quadros, alfa (0/1),
/// bytes estimados, fps × 1000]. Nulo sem composição.
AUREA_JNI jlongArray AUREA_FN(nativeImageExportPlan)(JNIEnv* env, jclass, jlong handle, jint format, jint shortSide,
                                                     jint maxWidth, jdouble fps, jboolean trimToContent) {
    NativeContext* c = ctx_of(handle);
    if (!c) return nullptr;
    const ImageExportSettings s = image_settings(format, shortSide, maxWidth, fps, trimToContent);
    const ImageExportPlan plan = c->engine.query_image_export_plan(s);
    if (plan.width == 0) return nullptr;
    const jlong v[6] = {static_cast<jlong>(plan.width), static_cast<jlong>(plan.height), static_cast<jlong>(plan.frames),
                        plan.alpha ? 1 : 0,
                        static_cast<jlong>(estimate_image_export_bytes(s.format, plan.width, plan.height, plan.frames, plan.alpha)),
                        static_cast<jlong>(std::llround(plan.fps * 1000.0))};
    jlongArray out = env->NewLongArray(6);
    if (out) env->SetLongArrayRegion(out, 0, 6, v);
    return out;
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

AUREA_JNI jint AUREA_FN(nativeLocalAiStatus)(JNIEnv*, jclass, jlong handle) {
    NativeContext* c = ctx_of(handle);
    return c ? static_cast<jint>(c->engine.local_ai_status()) : 0;
}

AUREA_JNI jint AUREA_FN(nativePreviewBufferRanges)(JNIEnv* env, jclass, jlong handle, jlongArray out) {
    NativeContext* c = ctx_of(handle);
    if (!c || !out) return 0;
    const auto capacity = static_cast<u32>(std::min<jsize>(aurea::kPreviewCacheMaxFrames, env->GetArrayLength(out) / 2));
    i64 ranges[aurea::kPreviewCacheMaxFrames * 2]{};
    const u32 count = c->engine.copy_preview_buffer_ranges(ranges, capacity);
    jlong encoded[aurea::kPreviewCacheMaxFrames * 2]{};
    for (u32 i = 0; i < count * 2; ++i) encoded[i] = static_cast<jlong>(ranges[i]);
    if (count) env->SetLongArrayRegion(out, 0, static_cast<jsize>(count * 2), encoded);
    return static_cast<jint>(count);
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
    return c && group >= 0 && path >= 0 && c->engine.ensure_vector_path_key(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(path))
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
    // "Marcar keyframe" no painel: com keyframe no cabeçote REGRAVA o valor
    // avaliado. Apagar continua no menu do keyframe, na timeline.
    return c && group >= 0 && param >= 0 && c->engine.ensure_vector_param_key(static_cast<u64>(layer), static_cast<u32>(group), static_cast<u32>(param))
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
    // Idem: marcar keyframe da forma regrava o valor do instante, nunca apaga.
    return c && param >= 0 && c->engine.ensure_shape_param_key(static_cast<u64>(layer), static_cast<u32>(param)) ? JNI_TRUE : JNI_FALSE;
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

AUREA_JNI jlong AUREA_FN(nativeImportPsd)(JNIEnv* env, jclass, jlong handle, jstring path, jstring name) {
    NativeContext* c=ctx_of(handle);
    if (!c) return -static_cast<jlong>(Errc::InvalidState);
    const auto p=to_string(env,path), n=to_string(env,name);
    return result_id(c->engine.import_psd(p,n.c_str()));
}

AUREA_JNI jstring AUREA_FN(nativeForegroundModelDirectory)(JNIEnv* env,jclass,jlong handle) {
    auto* c=ctx_of(handle);return env->NewStringUTF(c?c->engine.foreground_model_directory().c_str():"");
}
AUREA_JNI jlong AUREA_FN(nativeCreateGrid)(JNIEnv* env,jclass,jlong handle,jlongArray ids) {
    auto* c=ctx_of(handle);if(!c||!ids)return -1;
    const auto n=env->GetArrayLength(ids);if(n<1||n>512)return -1;
    std::vector<jlong> values(n);env->GetLongArrayRegion(ids,0,n,values.data());
    std::vector<u64> selected(values.begin(),values.end());return result_id(c->engine.create_grid(selected.data(),static_cast<u32>(n)));
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

AUREA_JNI jfloatArray AUREA_FN(nativeSceneSettings)(JNIEnv* env, jclass, jlong handle) {
    auto* c = ctx_of(handle); if (!c) return nullptr;
    float post[6]{}, floor[8]{};
    c->engine.query_scene3d_post(post); c->engine.query_scene_floor(floor);
    float values[8]{static_cast<float>(c->engine.studio_environment()), floor[0], post[0], post[1], post[2], post[3], post[4], post[5]};
    auto out = env->NewFloatArray(8); if (out) env->SetFloatArrayRegion(out, 0, 8, values); return out;
}
AUREA_JNI jboolean AUREA_FN(nativeSetSceneSetting)(JNIEnv*, jclass, jlong handle, jint parameter, jfloat value) {
    auto* c = ctx_of(handle); if (!c || !std::isfinite(value)) return false;
    auto& e = c->engine;
    float post[6]{}, floor[8]{};
    e.query_scene3d_post(post); e.query_scene_floor(floor);
    switch (parameter) {
    case 0: {
        if (!e.set_studio_environment(static_cast<aurea::u32>(value)).ok()) return false;
        return e.set_scene_floor(value > 0 ? 1 : 0, .18f, .18f, .18f, .2f, .5f);
    }
    case 1: return e.set_scene_floor(value > 0 ? 1 : 0, floor[1], floor[2], floor[3], floor[4], floor[5] > 0 ? floor[5] : .5f, floor[6], floor[7]);
    case 2: return e.set_scene3d_quality(static_cast<aurea::u32>(value));
    case 3: return e.set_scene3d_tonemap(static_cast<aurea::u32>(value), post[2]);
    case 4: return e.set_scene3d_tonemap(static_cast<aurea::u32>(post[1]), value);
    case 5: return e.set_scene3d_bloom(value > 0, post[4], post[5]);
    case 6: return e.set_scene3d_bloom(post[3] > 0, value, post[5]);
    default: return false;
    }
}
