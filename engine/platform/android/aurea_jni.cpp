// =============================================================================
//  Aurea / android / jni_bridge.cpp
//
//  A ponte entre Kotlin e o motor C++.
//
//  PRINCÍPIO: este arquivo é BURRO de propósito. Ele não decide nada, não
//  guarda estado além do necessário, e não faz política. Traduz tipos e chama o
//  motor. Toda regra vive no C++ compartilhado, onde o iOS também a executa —
//  duas implementações da mesma regra divergem na primeira correção que só uma
//  recebe.
//
//  O QUE ATRAVESSA, EM ORDEM DE VOLUME:
//
//   1. Comandos (UI → motor). Um bloco contíguo por frame, não uma chamada por
//      propriedade. `submit_commands` copia o buffer direto e devolve.
//
//   2. Status (motor → UI). Um struct POD de 256 bytes, escrito num buffer
//      direto que a UI já tem. Sem alocação, sem objeto do lado Java.
//
//   3. Consultas (listas de camadas, keyframes, curvas). Escrevem em buffers
//      diretos pelos mesmos motivos: uma data class por camada custaria 200
//      alocações por frame.
//
//  NENHUM BITMAP ATRAVESSA. O frame composto vai do compositor direto para o
//  `ANativeWindow`, e a UI nunca o vê. É essa ausência que mantém o pipeline
//  como zero-copy de ponta a ponta.
// =============================================================================

#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#include "aurea/Engine.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/bridge/BridgePods.hpp"

#include <cstring>
#include <string>

using namespace aurea;

namespace {

/// Sink de log do motor para o logcat. Instalado uma vez, na primeira chamada.
void android_log_sink(LogLevel level, const char* message, void* /*user*/) {
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

void ensure_log_sink() {
    static bool installed = false;
    if (!installed) {
        set_log_sink(&android_log_sink, nullptr);
        installed = true;
    }
}

/// A janela nativa adquirida em `initialize`.
///
/// POR QUE UM ESTÁTICO AQUI É CORRETO: existe UM motor por processo (a UI cria
/// uma instância e o ciclo de vida dela é o do app). E `ANativeWindow_fromSurface`
/// INCREMENTA uma referência que PRECISA ser devolvida com `ANativeWindow_release`
/// — esquecer a devolução vaza a superfície, e o SurfaceFlinger fica com um
/// buffer preso. Guardar o ponteiro aqui é o que permite liberá-lo no shutdown,
/// que é onde o ciclo fecha.
///
/// Se o motor deixar de ser único por processo, isto vira um mapa por handle —
/// e a asserção abaixo avisa antes disso acontecer.
ANativeWindow* g_nativeWindow = nullptr;
Engine*        g_owner = nullptr;

/// Resolve o `Engine*` de um handle. Devolve nullptr em handle inválido em vez
/// de desreferenciar — um bug na UI não pode derrubar o processo no C++.
[[nodiscard]] Engine* from_handle(jlong handle) noexcept {
    if (handle == 0) return nullptr;
    return reinterpret_cast<Engine*>(static_cast<intptr_t>(handle));
}

/// Ponteiro para o início de um `ByteBuffer` direto, ou nullptr.
[[nodiscard]] void* buffer_ptr(JNIEnv* env, jobject buffer) noexcept {
    if (!buffer) return nullptr;
    return env->GetDirectBufferAddress(buffer);
}

/// Tamanho em bytes de um `ByteBuffer` direto.
[[nodiscard]] jlong buffer_capacity(JNIEnv* env, jobject buffer) noexcept {
    if (!buffer) return 0;
    return env->GetDirectBufferCapacity(buffer);
}

/// Copia uma `jstring` para um `std::string` UTF-8.
[[nodiscard]] std::string to_string(JNIEnv* env, jstring s) noexcept {
    if (!s) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    if (!chars) return {};
    std::string result(chars);
    env->ReleaseStringUTFChars(s, chars);
    return result;
}

/// Traz uma referência curta para o ambiente JNI. O parâmetro precisa se
/// chamar `env_` na assinatura (o JNI casa por nome), então o corpo usa `env`.
///
/// O `(void)env` existe porque várias funções deste arquivo não usam o
/// ambiente — e sem ele o compilador avisaria de variável não usada, o que
/// treinaria quem lê a ignorar avisos.
#define AUREA_JNI_ENTER() JNIEnv* env = env_; (void)env

/// Resolve o motor e sai cedo quando o handle é inválido. Um handle morto
/// vindo da UI é bug dela, e não pode derrubar o processo no lado nativo.
///
/// Duas variantes porque uma função que devolve `void` não aceita
/// `return {}` — o compilador recusa, e a recusa é o lembrete de que a versão
/// vazia existe.
#define AUREA_JNI_RESOLVE(handle)          \
    Engine* engine = from_handle(handle);  \
    if (!engine) return {}

#define AUREA_JNI_GUARD(handle) AUREA_JNI_RESOLVE(handle)
#define AUREA_JNI_GUARD_VOID(handle) \
    Engine* engine = from_handle(handle); \
    if (!engine) return

} // namespace

#define AUREA_JNI_EXPORT extern "C" JNIEXPORT

// =============================================================================
// Ciclo de vida
// =============================================================================
AUREA_JNI_EXPORT jlong JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeCreate(JNIEnv* env_, jclass) {
    AUREA_JNI_ENTER();
    (void)env;
    ensure_log_sink();

    // `new` numa fronteira C++ compilada sem exceções: se o operador falhar,
    // devolve nullptr em vez de lançar. E o motor NUNCA lança para o Kotlin —
    // uma exceção atravessando o JNI é comportamento indefinido.
    Engine* engine = new (std::nothrow) Engine();
    if (!engine) {
        AUREA_LOG_FATAL("nao foi possivel alocar o motor");
        return 0;
    }
    AUREA_LOG_INFO("motor criado (v%s)", AUREA_VERSION_LABEL);
    return static_cast<jlong>(reinterpret_cast<intptr_t>(engine));
}

AUREA_JNI_EXPORT void JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeDestroy(JNIEnv* env_, jclass, jlong handle) {
    AUREA_JNI_ENTER();
    (void)env;
    Engine* engine = from_handle(handle);
    if (!engine) return;

    // Fecha o ciclo da janela nativa: `initialize` a adquiriu, aqui ela volta.
    if (g_owner == engine || g_owner == nullptr) {
        if (g_nativeWindow) {
            ANativeWindow_release(g_nativeWindow);
            g_nativeWindow = nullptr;
        }
        g_owner = nullptr;
    }

    delete engine;
    AUREA_LOG_INFO("motor destruido");
}

AUREA_JNI_EXPORT jboolean JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeInitialize(
    JNIEnv* env_, jclass, jlong handle, jobject surface,
    jint width, jint height, jfloat refreshRate, jstring cacheDir, jstring documentsDir) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);

    if (!surface) {
        AUREA_LOG_ERROR("initialize sem Surface");
        return JNI_FALSE;
    }

    // A janela nativa é a superfície de apresentação do motor. `Acquire`
    // incrementa a referência; `Release` no shutdown. Guardar o ANativeWindow
    // sem adquirir produziria um ponteiro morto quando o Surface for destruído
    // (rotação, app em background) — e o sintoma seria um crash dentro do
    // Vulkan, longe da causa.
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        AUREA_LOG_ERROR("nao foi possivel obter o ANativeWindow");
        return JNI_FALSE;
    }
    if (g_owner && g_owner != engine) {
        // Dois motores no mesmo processo: o estático que guarda a janela
        // deixaria de ser correto. Recusar é melhor do que vazar a superfície
        // do primeiro em silêncio.
        AUREA_LOG_ERROR("segundo motor no mesmo processo: janela nativa nao suportada");
        ANativeWindow_release(window);
        return JNI_FALSE;
    }
    g_nativeWindow = window;
    g_owner = engine;

    EngineConfig config;
    config.nativeWindow = window;
    config.surfaceWidth = static_cast<u32>(width);
    config.surfaceHeight = static_cast<u32>(height);
    config.displayRefreshRate = refreshRate > 0.0f ? refreshRate : 60.0f;
    config.createGpuBackend = true;
    config.cacheDirectory = to_string(env, cacheDir);
    config.documentsDirectory = to_string(env, documentsDir);
    // Telemetria ligada em debug: os contadores custam pouco e são a única
    // forma de responder "por que o preview está lento" sem adivinhar.
#if !defined(NDEBUG)
    config.enableTelemetry = true;
#endif

    const Status s = engine->initialize(config);
    if (!s.ok()) {
        AUREA_LOG_ERROR("falha ao inicializar: %s", s.message().data());
        ANativeWindow_release(window);
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

AUREA_JNI_EXPORT void JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeShutdown(JNIEnv* env_, jclass, jlong handle) {
    AUREA_JNI_ENTER();
    (void)env;
    Engine* engine = from_handle(handle);
    if (!engine) return;
    engine->shutdown();
}

// =============================================================================
// A fronteira por frame
// =============================================================================
AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeSubmitCommands(
    JNIEnv* env_, jclass, jlong handle, jobject commandBuffer, jint count,
    jobject stringBlob, jint stringBlobSize) {
    AUREA_JNI_ENTER();
    if (!from_handle(handle)) return 0;
    Engine* engine = from_handle(handle);

    auto* commands = static_cast<const Command*>(buffer_ptr(env, commandBuffer));
    if (!commands || count <= 0) return 0;

    // Confere o tamanho do buffer antes de confiar no `count`. Se a UI passar um
    // número maior do que o buffer comporta, o motor leria além do fim — e uma
    // leitura fora do buffer é um bug que só aparece sob carga.
    const jlong capacity = buffer_capacity(env, commandBuffer);
    if (capacity < static_cast<jlong>(count) * static_cast<jlong>(sizeof(Command))) {
        AUREA_LOG_ERROR("lote de comandos maior que o buffer (%d comandos, %lld bytes)",
                        static_cast<int>(count), static_cast<long long>(capacity));
        return 0;
    }

    const auto* blob = static_cast<const char*>(buffer_ptr(env, stringBlob));
    return static_cast<jint>(
        engine->submit_commands(commands, static_cast<u32>(count), blob,
                                static_cast<u32>(stringBlobSize)));
}

AUREA_JNI_EXPORT jboolean JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeRenderFrame(JNIEnv* env_, jclass,
                                                          jlong handle, jlong audioTimeNs) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);

    const Status s = engine->render_frame(TickNs{static_cast<i64>(audioTimeNs)});
    // O erro é registrado, mas NÃO derruba o app. Um frame que não saiu é um
    // problema a ser mostrado na telemetria; matar o editor no meio de um
    // projeto de duas horas seria muito pior.
    if (!s.ok()) {
        AUREA_LOG_WARN("frame nao renderizou: %s", s.message().data());
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

AUREA_JNI_EXPORT jboolean JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeReadStatus(JNIEnv* env_, jclass,
                                                         jlong handle, jobject out) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);

    auto* pod = static_cast<bridge::EngineStatusPOD*>(buffer_ptr(env, out));
    if (!pod) return JNI_FALSE;
    // O buffer direto pode ser maior que o struct; escrever só no tamanho exato
    // evita tocar em memória que não é nossa.
    if (buffer_capacity(env, out) < static_cast<jlong>(sizeof(bridge::EngineStatusPOD))) {
        return JNI_FALSE;
    }
    engine->fill_status(*pod);
    return JNI_TRUE;
}

AUREA_JNI_EXPORT jboolean JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeReadTelemetry(JNIEnv* env_, jclass,
                                                            jlong handle, jobject out) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);

    auto* pod = static_cast<bridge::TelemetryPOD*>(buffer_ptr(env, out));
    if (!pod) return JNI_FALSE;
    if (buffer_capacity(env, out) < static_cast<jlong>(sizeof(bridge::TelemetryPOD))) {
        return JNI_FALSE;
    }
    engine->fill_telemetry(*pod);
    return JNI_TRUE;
}

// =============================================================================
// Consultas
// =============================================================================
AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeQueryLayers(
    JNIEnv* env_, jclass, jlong handle, jobject array, jint capacity,
    jobject nameBlob, jint nameBlobCapacity) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);

    auto* rows = static_cast<bridge::LayerRow*>(buffer_ptr(env, array));
    auto* blob = static_cast<char*>(buffer_ptr(env, nameBlob));
    if (!rows || capacity <= 0) return 0;

    return static_cast<jint>(engine->query_layers(
        rows, static_cast<u32>(capacity), blob,
        static_cast<u32>(nameBlobCapacity > 0 ? nameBlobCapacity : 0)));
}

AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeQueryKeyframes(
    JNIEnv* env_, jclass, jlong handle, jlong layerHandle, jobject array, jint capacity) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);

    auto* rows = static_cast<bridge::KeyframeRow*>(buffer_ptr(env, array));
    if (!rows || capacity <= 0) return 0;

    return static_cast<jint>(engine->query_keyframes(
        static_cast<u64>(layerHandle), rows, static_cast<u32>(capacity)));
}

AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeQueryCurve(
    JNIEnv* env_, jclass, jlong handle, jlong layerHandle, jint property,
    jint from, jint to, jfloatArray out, jint count) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);
    if (!out || count <= 0) return 0;

    // Aqui o buffer não é direto (é um FloatArray do Kotlin). Copia uma vez:
    // a curva é amostrada sob demanda, quando o editor de gráfico está aberto —
    // não no caminho de frame.
    jfloat* values = env->GetFloatArrayElements(out, nullptr);
    if (!values) return 0;

    const u32 written = engine->query_curve(
        static_cast<u64>(layerHandle), static_cast<u32>(property), from, to,
        reinterpret_cast<f32*>(values), static_cast<u32>(count));

    env->ReleaseFloatArrayElements(out, values, 0);
    return static_cast<jint>(written);
}

AUREA_JNI_EXPORT void JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeSetSelection(
    JNIEnv* env_, jclass, jlong handle, jlongArray handles) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD_VOID(handle);
    if (!handles) {
        engine->clear_selection();
        return;
    }

    const jsize count = env->GetArrayLength(handles);
    if (count == 0) {
        engine->clear_selection();
        return;
    }

    jlong* raw = env->GetLongArrayElements(handles, nullptr);
    if (!raw) return;

    // `u64` e `jlong` são ambos 64 bits e com sinal/unsigned compatível na
    // prática, mas o cast explícito documenta a intenção em vez de depender
    // disso silenciosamente.
    engine->set_selection(reinterpret_cast<const u64*>(raw), static_cast<u32>(count));
    env->ReleaseLongArrayElements(handles, raw, JNI_ABORT);
}

AUREA_JNI_EXPORT void JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeClearSelection(JNIEnv* env_, jclass, jlong handle) {
    AUREA_JNI_ENTER();
    (void)env;
    if (Engine* engine = from_handle(handle)) engine->clear_selection();
}

// =============================================================================
// Superfície
// =============================================================================
AUREA_JNI_EXPORT void JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeResizeSurface(
    JNIEnv* env_, jclass, jlong handle, jint width, jint height) {
    AUREA_JNI_ENTER();
    (void)env;
    if (Engine* engine = from_handle(handle)) {
        (void)engine->resize_surface(static_cast<u32>(width), static_cast<u32>(height));
    }
}

AUREA_JNI_EXPORT void JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeSuspend(JNIEnv* env_, jclass, jlong handle) {
    AUREA_JNI_ENTER();
    (void)env;
    if (Engine* engine = from_handle(handle)) {
        (void)engine->suspend();
    }
}

AUREA_JNI_EXPORT void JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeResume(
    JNIEnv* env_, jclass, jlong handle, jobject surface, jint width, jint height) {
    AUREA_JNI_ENTER();
    (void)env;
    Engine* engine = from_handle(handle);
    if (!engine || !surface) return;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return;

    // A janela antiga precisa ser devolvida ANTES de trocar: suspender liberou a
    // GPU mas não a referência, e guardar as duas vazaria uma superfície por
    // ciclo de background. O SurfaceFlinger segura os buffers e a memória
    // gráfica do app cresce a cada ida ao background.
    if (g_nativeWindow && g_nativeWindow != window) {
        ANativeWindow_release(g_nativeWindow);
    }
    g_nativeWindow = window;
    g_owner = engine;

    EngineConfig config;
    config.nativeWindow = window;
    config.surfaceWidth = static_cast<u32>(width);
    config.surfaceHeight = static_cast<u32>(height);
    (void)engine->resume(config);
}

// =============================================================================
// Projeto
// =============================================================================
AUREA_JNI_EXPORT jboolean JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeNewProject(
    JNIEnv* env_, jclass, jlong handle, jint width, jint height, jfloat fps, jstring title) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);

    const std::string t = to_string(env, title);
    return engine->new_project(static_cast<u32>(width), static_cast<u32>(height),
                               static_cast<f64>(fps), t.c_str()).ok() ? JNI_TRUE : JNI_FALSE;
}

AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeLoadProject(JNIEnv* env_, jclass,
                                                          jlong handle, jstring path) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);
    const std::string p = to_string(env, path);
    return static_cast<jint>(engine->load_project(p.c_str()).raw());
}

AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeSaveProject(JNIEnv* env_, jclass,
                                                          jlong handle, jstring path) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);
    const std::string p = to_string(env, path);
    return static_cast<jint>(engine->save_project(p.c_str()).raw());
}

AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeDiscardRecovery(JNIEnv* env_, jclass, jlong handle) {
    AUREA_JNI_ENTER();
    Engine* engine = from_handle(handle);
    if (!engine) return static_cast<jint>(Errc::InvalidState);
    engine->discard_recovery();
    return static_cast<jint>(Errc::Ok);
}

AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeRecoverSession(JNIEnv* env_, jclass, jlong handle) {
    AUREA_JNI_ENTER();
    Engine* engine = from_handle(handle);
    if (!engine) return static_cast<jint>(Errc::InvalidState);
    return static_cast<jint>(engine->recover_session().raw());
}

// =============================================================================
// Export
// =============================================================================
AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeStartExport(JNIEnv* env_, jclass,
                                                          jlong handle, jstring outputPath) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);
    const std::string p = to_string(env, outputPath);
    const ExportSettings settings;   // os ajustes vigentes vêm do projeto
    return static_cast<jint>(engine->start_export(settings, p.c_str()).raw());
}

AUREA_JNI_EXPORT jint JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeCancelExport(JNIEnv* env_, jclass, jlong handle) {
    AUREA_JNI_ENTER();
    (void)env;
    Engine* engine = from_handle(handle);
    if (!engine) return static_cast<jint>(Errc::InvalidState);
    return static_cast<jint>(engine->cancel_export().raw());
}

AUREA_JNI_EXPORT jboolean JNICALL
Java_com_aurea_aurea_engine_AureaEngine_nativeExportProgress(JNIEnv* env_, jclass,
                                                             jlong handle, jobject out) {
    AUREA_JNI_ENTER();
    AUREA_JNI_GUARD(handle);

    auto* pod = static_cast<bridge::ExportProgressPOD*>(buffer_ptr(env, out));
    if (!pod) return JNI_FALSE;
    if (buffer_capacity(env, out) < static_cast<jlong>(sizeof(bridge::ExportProgressPOD))) {
        return JNI_FALSE;
    }
    engine->fill_export_progress(*pod);
    return JNI_TRUE;
}
