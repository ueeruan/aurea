#include "aurea/export/AndroidVideoEncoder.hpp"

#if defined(__ANDROID__)

#include "AndroidCodecName.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <sys/system_properties.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

extern "C" {
#include <libavcodec/jni.h>
}

namespace aurea::android_video {
namespace {

// MediaCodecInfo.CodecCapabilities / MediaFormat (valores públicos do SDK).
constexpr i32 kColorSurface = 0x7F000789;
constexpr i32 kColorFlexible = 0x7F420888;
constexpr i32 kColorPlanar = 0x13;
constexpr i32 kColorPackedPlanar = 0x14;
constexpr i32 kColorSemiPlanar = 0x15;
constexpr i32 kColorPackedSemiPlanar = 0x27;
constexpr i32 kBitrateVbr = 1, kBitrateCbr = 2;
constexpr i32 kAvcBaseline = 1, kAvcConstrainedBaseline = 0x10000;
constexpr u32 kFlagKey = 1, kFlagConfig = 2, kFlagEos = 4;
constexpr u32 kSurfaceInFlight = 3;

bool jni_enter(JavaVM*& vm, JNIEnv*& env, bool& detach) noexcept {
    vm = static_cast<JavaVM*>(av_jni_get_java_vm(nullptr));
    env = nullptr;
    detach = false;
    if (!vm) return false;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        detach = true;
    } else if (state != JNI_OK || !env) {
        return false;
    }
    if (env->PushLocalFrame(96) < 0) {
        env->ExceptionClear();
        if (detach) vm->DetachCurrentThread();
        return false;
    }
    return true;
}

void jni_leave(JavaVM* vm, JNIEnv* env, bool detach) noexcept {
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->PopLocalFrame(nullptr);
    if (detach) vm->DetachCurrentThread();
}

// Shaders do degrau Surface: NV12 (a mesma conversão do yuv_encode.frag, ao
// contrário) → R'G'B' de 8 bits na Surface do encoder. O encoder faz o
// R'G'B' → Y'CbCr com o padrão de cor configurado no MediaFormat; o sink
// confere o primeiro quadro decodificado contra o NV12 original.
constexpr const char* kVertex =
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = vec2((a_pos.x + 1.0) * 0.5, (1.0 - a_pos.y) * 0.5);\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";
constexpr const char* kFragment =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_y;\n"
    "uniform sampler2D u_c;\n"
    "uniform vec4 u_m;\n"   // x = kr, y = kb, z = faixa cheia (1) ou limitada (0)
    "void main() {\n"
    "    float yv = texture2D(u_y, v_uv).r;\n"
    "    vec2 cv = texture2D(u_c, v_uv).ra;\n"
    "    float Y; float Cb; float Cr;\n"
    "    if (u_m.z > 0.5) {\n"
    "        Y = yv; Cb = cv.x - 128.0 / 255.0; Cr = cv.y - 128.0 / 255.0;\n"
    "    } else {\n"
    "        Y = (yv * 255.0 - 16.0) / 219.0;\n"
    "        Cb = (cv.x * 255.0 - 128.0) / 224.0;\n"
    "        Cr = (cv.y * 255.0 - 128.0) / 224.0;\n"
    "    }\n"
    "    float kr = u_m.x; float kb = u_m.y; float kg = 1.0 - kr - kb;\n"
    "    float R = Y + 2.0 * (1.0 - kr) * Cr;\n"
    "    float B = Y + 2.0 * (1.0 - kb) * Cb;\n"
    "    float G = (Y - kr * R - kb * B) / kg;\n"
    "    gl_FragColor = vec4(clamp(vec3(R, G, B), 0.0, 1.0), 1.0);\n"
    "}\n";
// surface-direct: o quadro já vem em R'G'B' de 8 bits (export/rgb_encode.frag),
// do mesmo tamanho da Surface; amostra 1:1, sem filtro.
constexpr const char* kDirectFragment =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_rgb;\n"
    "void main() { gl_FragColor = vec4(texture2D(u_rgb, v_uv).rgb, 1.0); }\n";

// MediaFormat: padrão/faixa/curva a partir das etiquetas do arquivo (H.273).
i32 color_standard(u8 matrix) noexcept {
    switch (matrix) {
        case 1: return 1;            // COLOR_STANDARD_BT709
        case 5: return 2;            // BT601_PAL
        case 6: return 4;            // BT601_NTSC
        case 9: case 10: return 6;   // BT2020
        default: return 0;
    }
}
i32 color_transfer(u8 transfer) noexcept {
    switch (transfer) {
        case 1: case 6: case 14: case 15: return 3;  // SDR_VIDEO
        case 8: return 1;                             // LINEAR
        case 16: return 6;                            // ST2084
        case 18: return 7;                            // HLG
        default: return 0;
    }
}

} // namespace

bool debug_flag(const char* property, const char* token) noexcept {
    char value[PROP_VALUE_MAX + 1]{};
    if (__system_property_get(property, value) <= 0) return false;
    if (!token) return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0;
    const usize n = std::strlen(token);
    for (const char* p = value; *p;) {
        const char* end = std::strchr(p, ',');
        const usize len = end ? static_cast<usize>(end - p) : std::strlen(p);
        if (len == n && std::strncmp(p, token, n) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

bool find_encoder(bool hevc, u32 width, u32 height, f64 fps, bool hardware, CodecInfo& out) noexcept {
    out = CodecInfo{};
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    bool detach = false;
    if (!jni_enter(vm, env, detach)) return false;
    bool found = false;
    do {
        const jclass version = env->FindClass("android/os/Build$VERSION");
        if (!version) break;
        const jfieldID sdk = env->GetStaticFieldID(version, "SDK_INT", "I");
        if (!sdk) break;
        // Android 8/9: sem isHardwareAccelerated/isSoftwareOnly (API 29); o nome
        // do codec classifica (AndroidCodecName.hpp). Antes desistia aqui e o
        // export ia direto para o software.
        const bool modern = env->GetStaticIntField(version, sdk) >= 29;
        const jclass listClass = env->FindClass("android/media/MediaCodecList");
        const jclass infoClass = env->FindClass("android/media/MediaCodecInfo");
        const jclass capClass = env->FindClass("android/media/MediaCodecInfo$CodecCapabilities");
        const jclass videoClass = env->FindClass("android/media/MediaCodecInfo$VideoCapabilities");
        const jclass encClass = env->FindClass("android/media/MediaCodecInfo$EncoderCapabilities");
        const jclass levelClass = env->FindClass("android/media/MediaCodecInfo$CodecProfileLevel");
        if (!listClass || !infoClass || !capClass || !videoClass || !encClass || !levelClass) break;
        const auto ctor = env->GetMethodID(listClass, "<init>", "(I)V");
        const auto infos = env->GetMethodID(listClass, "getCodecInfos", "()[Landroid/media/MediaCodecInfo;");
        const auto isEncoder = env->GetMethodID(infoClass, "isEncoder", "()Z");
        const auto isHardware = modern ? env->GetMethodID(infoClass, "isHardwareAccelerated", "()Z") : nullptr;
        const auto isSoftware = modern ? env->GetMethodID(infoClass, "isSoftwareOnly", "()Z") : nullptr;
        const auto types = env->GetMethodID(infoClass, "getSupportedTypes", "()[Ljava/lang/String;");
        const auto name = env->GetMethodID(infoClass, "getName", "()Ljava/lang/String;");
        const auto capabilities = env->GetMethodID(infoClass, "getCapabilitiesForType",
            "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");
        const auto videoCaps = env->GetMethodID(capClass, "getVideoCapabilities",
            "()Landroid/media/MediaCodecInfo$VideoCapabilities;");
        const auto encCaps = env->GetMethodID(capClass, "getEncoderCapabilities",
            "()Landroid/media/MediaCodecInfo$EncoderCapabilities;");
        const auto supports = env->GetMethodID(videoClass, "areSizeAndRateSupported", "(IID)Z");
        const auto bitrateMode = env->GetMethodID(encClass, "isBitrateModeSupported", "(I)Z");
        const auto formatsField = env->GetFieldID(capClass, "colorFormats", "[I");
        const auto levelsField = env->GetFieldID(capClass, "profileLevels",
            "[Landroid/media/MediaCodecInfo$CodecProfileLevel;");
        const auto profileField = env->GetFieldID(levelClass, "profile", "I");
        const auto levelField = env->GetFieldID(levelClass, "level", "I");
        if (!ctor || !infos || !isEncoder || (modern && (!isHardware || !isSoftware)) || !types || !name || !capabilities ||
            !videoCaps || !encCaps || !supports || !bitrateMode || !formatsField || !levelsField ||
            !profileField || !levelField) break;
        const jobject list = env->NewObject(listClass, ctor, 1 /* ALL_CODECS */);
        if (!list || env->ExceptionCheck()) break;
        const auto array = static_cast<jobjectArray>(env->CallObjectMethod(list, infos));
        if (!array || env->ExceptionCheck()) break;
        const char* mime = hevc ? "video/hevc" : "video/avc";
        const jsize count = env->GetArrayLength(array);
        for (jsize i = 0; i < count && !found; ++i) {
            if (env->PushLocalFrame(32) < 0) { env->ExceptionClear(); break; }
            const jobject info = env->GetObjectArrayElement(array, i);
            const auto softwareByName = [&]() {
                const auto codec = static_cast<jstring>(env->CallObjectMethod(info, name));
                if (!codec || env->ExceptionCheck()) { env->ExceptionClear(); return true; }
                const char* text = env->GetStringUTFChars(codec, nullptr);
                const bool software = android_codec::software_by_name(text);
                if (text) env->ReleaseStringUTFChars(codec, text);
                return software;
            };
            const bool classified = info && env->CallBooleanMethod(info, isEncoder) && !env->ExceptionCheck() &&
                (modern ? (hardware ? env->CallBooleanMethod(info, isHardware) : env->CallBooleanMethod(info, isSoftware))
                        : (hardware ? !softwareByName() : softwareByName())) &&
                !env->ExceptionCheck();
            const auto supported = classified ? static_cast<jobjectArray>(env->CallObjectMethod(info, types)) : nullptr;
            for (jsize t = 0; supported && !env->ExceptionCheck() && t < env->GetArrayLength(supported); ++t) {
                const auto type = static_cast<jstring>(env->GetObjectArrayElement(supported, t));
                const char* text = type ? env->GetStringUTFChars(type, nullptr) : nullptr;
                const bool matches = text && std::strcmp(text, mime) == 0;
                if (text) env->ReleaseStringUTFChars(type, text);
                if (!matches) { if (type) env->DeleteLocalRef(type); continue; }
                const jobject cap = env->CallObjectMethod(info, capabilities, type);
                const jobject vc = cap && !env->ExceptionCheck() ? env->CallObjectMethod(cap, videoCaps) : nullptr;
                if (!vc || env->ExceptionCheck() ||
                    !env->CallBooleanMethod(vc, supports, static_cast<jint>(width), static_cast<jint>(height), fps) ||
                    env->ExceptionCheck()) break;
                const auto codecName = static_cast<jstring>(env->CallObjectMethod(info, name));
                const char* textName = codecName && !env->ExceptionCheck() ? env->GetStringUTFChars(codecName, nullptr) : nullptr;
                if (!textName || std::strlen(textName) >= sizeof(out.name)) {
                    if (textName) env->ReleaseStringUTFChars(codecName, textName);
                    break;
                }
                std::snprintf(out.name, sizeof(out.name), "%s", textName);
                env->ReleaseStringUTFChars(codecName, textName);
                out.hardware = hardware;
                if (const auto formats = static_cast<jintArray>(env->GetObjectField(cap, formatsField)); formats) {
                    const jsize n = env->GetArrayLength(formats);
                    try { out.colorFormats.resize(static_cast<usize>(n)); } catch (...) { out.colorFormats.clear(); }
                    if (!out.colorFormats.empty())
                        env->GetIntArrayRegion(formats, 0, n, reinterpret_cast<jint*>(out.colorFormats.data()));
                }
                out.surface = std::find(out.colorFormats.begin(), out.colorFormats.end(), kColorSurface) != out.colorFormats.end();
                if (const jobject enc = env->CallObjectMethod(cap, encCaps); enc && !env->ExceptionCheck()) {
                    out.vbr = env->CallBooleanMethod(enc, bitrateMode, kBitrateVbr) && !env->ExceptionCheck();
                    out.cbr = env->CallBooleanMethod(enc, bitrateMode, kBitrateCbr) && !env->ExceptionCheck();
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                if (const auto levels = static_cast<jobjectArray>(env->GetObjectField(cap, levelsField)); levels && !hevc) {
                    for (jsize l = 0; l < env->GetArrayLength(levels); ++l) {
                        const jobject pl = env->GetObjectArrayElement(levels, l);
                        if (!pl) continue;
                        const jint profile = env->GetIntField(pl, profileField);
                        const jint level = env->GetIntField(pl, levelField);
                        env->DeleteLocalRef(pl);
                        // Baseline "puro" é o que o caminho antigo pedia; o
                        // Constrained Baseline só quando é o único anunciado.
                        if (profile == kAvcBaseline || (profile == kAvcConstrainedBaseline && out.baselineProfile != kAvcBaseline)) {
                            if (profile != out.baselineProfile) out.baselineLevel = 0;
                            out.baselineProfile = profile;
                            out.baselineLevel = std::max<i32>(out.baselineLevel, level);
                        }
                    }
                }
                found = true;
                break;
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->PopLocalFrame(nullptr);
        }
    } while (false);
    if (!found) out = CodecInfo{};
    jni_leave(vm, env, detach);
    return found;
}

// O lado GLES do degrau Surface vive numa thread própria: todo EGL/GL é
// feito nela e quem chama espera com prazo. Um driver que trava DENTRO do
// eglSwapBuffers (visto no emulador; possível em aparelho) prende só esta
// thread — o sink desiste do degrau e segue no próximo, sem quarentena.
struct SurfaceFeeder {
    enum class Job : u8 { None, Init, Frame, Quit };
    std::mutex mutex;
    std::condition_variable wake, finished;
    Job job = Job::None;
    u64 serial = 0, completed = 0;
    bool abandoned = false;
    Status result = OkStatus;
    char detail[256]{};
    // Entrada (copiada pelo chamador com o alimentador parado).
    ANativeWindow* window = nullptr;   ///< referência própria
    u32 width = 0, height = 0;
    f32 kr = 0.2126f, kb = 0.0722f;
    bool fullRange = false;
    std::vector<u8> pixels;            ///< Y e depois CbCr, contíguos
    i64 ptsUs = 0;
    bool simulateHang = false;         ///< teste: o driver "trava" no quadro (debug.aurea.enc.fail=surface-hang)
    // EGL/GL: só a thread do alimentador toca.
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    PFNEGLPRESENTATIONTIMEANDROIDPROC presentation = nullptr;
    GLuint program = 0, texY = 0, texC = 0;
    GLint position = -1, matrix = -1, samplerY = -1, samplerC = -1;
    // surface-direct: cada AHardwareBuffer do anel do motor vira um EGLImage
    // (uma vez; o anel tem até 4 slots) com referência própria até o teardown.
    bool direct = false;
    void* directBuffer = nullptr;      ///< o buffer do quadro atual (copiado pelo chamador)
    GLuint directProgram = 0;
    GLint directPosition = -1, directSampler = -1;
    struct DirectImage { AHardwareBuffer* buffer = nullptr; EGLImageKHR image = EGL_NO_IMAGE_KHR; GLuint texture = 0; };
    std::vector<DirectImage> images;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC nativeClient = nullptr;
    PFNEGLCREATEIMAGEKHRPROC createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTarget = nullptr;

    Status fail(Errc code, const char* format, ...) noexcept __attribute__((format(printf, 3, 4))) {
        va_list args;
        va_start(args, format);
        std::vsnprintf(detail, sizeof(detail), format, args);
        va_end(args);
        return Status{code, detail};
    }

    static GLuint compile(GLenum type, const char* source) noexcept {
        const GLuint shader = glCreateShader(type);
        if (!shader) return 0;
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[160]{};
            glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
            AUREA_LOG_WARN("encoder surface: shader nao compilou: %s", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    Status init() noexcept {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        // Nunca eglTerminate: o display é do processo (o backend GLES do motor
        // e a interface usam o mesmo).
        if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
            return fail(Errc::EncoderUnavailable, "EGL indisponivel para a Surface do encoder");
        EGLConfig config = nullptr;
        EGLint found = 0;
        for (const EGLint alpha : {8, 0}) {
            const EGLint attributes[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, alpha,
                                         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                         EGL_RECORDABLE_ANDROID, EGL_TRUE, EGL_NONE};
            if (eglChooseConfig(display, attributes, &config, 1, &found) && found > 0 && config) break;
            config = nullptr;
        }
        if (!config) return fail(Errc::EncoderUnavailable, "EGL sem configuracao gravavel (EGL_RECORDABLE_ANDROID)");
        const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
        if (context == EGL_NO_CONTEXT)
            return fail(Errc::EncoderUnavailable, "EGL nao criou contexto para o encoder (0x%x)", eglGetError());
        const EGLint surfaceAttributes[] = {EGL_NONE};
        surface = eglCreateWindowSurface(display, config, window, surfaceAttributes);
        if (surface == EGL_NO_SURFACE)
            return fail(Errc::EncoderUnavailable, "EGL nao abriu a Surface do encoder (0x%x)", eglGetError());
        presentation = reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(eglGetProcAddress("eglPresentationTimeANDROID"));
        if (!presentation) return fail(Errc::EncoderUnavailable, "EGL sem eglPresentationTimeANDROID");
        EGLint sw = 0, sh = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &sw);
        eglQuerySurface(display, surface, EGL_HEIGHT, &sh);
        if (sw != static_cast<EGLint>(width) || sh != static_cast<EGLint>(height))
            return fail(Errc::EncoderUnavailable, "Surface do encoder com %dx%d em vez de %ux%u", sw, sh, width, height);
        if (!eglMakeCurrent(display, surface, surface, context))
            return fail(Errc::EncoderUnavailable, "EGL nao ativou o contexto do encoder (0x%x)", eglGetError());
        const GLuint vs = compile(GL_VERTEX_SHADER, kVertex);
        const GLuint fs = compile(GL_FRAGMENT_SHADER, kFragment);
        if (vs && fs) {
            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glBindAttribLocation(program, 0, "a_pos");
            glLinkProgram(program);
            GLint linked = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (!linked) { glDeleteProgram(program); program = 0; }
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        if (program) {
            position = glGetAttribLocation(program, "a_pos");
            matrix = glGetUniformLocation(program, "u_m");
            samplerY = glGetUniformLocation(program, "u_y");
            samplerC = glGetUniformLocation(program, "u_c");
            GLuint textures[2]{};
            glGenTextures(2, textures);
            texY = textures[0];
            texC = textures[1];
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glBindTexture(GL_TEXTURE_2D, texY);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                         GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
            glBindTexture(GL_TEXTURE_2D, texC);
            // Croma no centro do bloco 2×2 (como o yuv_encode.frag a calcula):
            // a amostragem bilinear na posição do pixel reconstrói essa posição.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, static_cast<GLsizei>(width / 2),
                         static_cast<GLsizei>(height / 2), 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, nullptr);
            glDisable(GL_DITHER);
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        }
        const GLenum error = glGetError();
        if (!program || !texY || !texC || error != GL_NO_ERROR)
            return fail(Errc::EncoderUnavailable, "GLES nao preparou a conversao da Surface (0x%x)", error);
        if (direct) return init_direct();
        return OkStatus;
    }

    /// surface-direct: EGLImage de AHardwareBuffer → textura → quad. Sem as
    /// extensões, o degrau falha na abertura e a escada segue no surface NV12.
    Status init_direct() noexcept {
        const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
        const char* gl = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (!extensions || !std::strstr(extensions, "EGL_ANDROID_image_native_buffer") ||
            !std::strstr(extensions, "EGL_KHR_image_base") || !gl || !std::strstr(gl, "GL_OES_EGL_image"))
            return fail(Errc::NotSupported, "EGL/GLES sem EGLImage de AHardwareBuffer");
        nativeClient = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(eglGetProcAddress("eglGetNativeClientBufferANDROID"));
        createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
        destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
        imageTarget = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        if (!nativeClient || !createImage || !destroyImage || !imageTarget)
            return fail(Errc::NotSupported, "EGL sem as funcoes de EGLImage de AHardwareBuffer");
        const GLuint vs = compile(GL_VERTEX_SHADER, kVertex);
        const GLuint fs = compile(GL_FRAGMENT_SHADER, kDirectFragment);
        if (vs && fs) {
            directProgram = glCreateProgram();
            glAttachShader(directProgram, vs);
            glAttachShader(directProgram, fs);
            glBindAttribLocation(directProgram, 0, "a_pos");
            glLinkProgram(directProgram);
            GLint linked = 0;
            glGetProgramiv(directProgram, GL_LINK_STATUS, &linked);
            if (!linked) { glDeleteProgram(directProgram); directProgram = 0; }
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        if (!directProgram) return fail(Errc::EncoderUnavailable, "GLES nao compilou o desenho direto da Surface");
        directPosition = glGetAttribLocation(directProgram, "a_pos");
        directSampler = glGetUniformLocation(directProgram, "u_rgb");
        return OkStatus;
    }

    /// A textura GLES do buffer do anel (importada uma vez, com referência própria).
    GLuint direct_texture(AHardwareBuffer* buffer) noexcept {
        for (const DirectImage& image : images) if (image.buffer == buffer) return image.texture;
        if (images.size() >= 8) return 0;   // o anel do motor tem até 4 slots
        AHardwareBuffer_Desc desc{};
        AHardwareBuffer_describe(buffer, &desc);
        if (desc.width != width || desc.height != height) return 0;
        const EGLClientBuffer client = nativeClient(buffer);
        if (!client) return 0;
        const EGLint attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        const EGLImageKHR image = createImage(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client, attributes);
        if (image == EGL_NO_IMAGE_KHR) return 0;
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        imageTarget(GL_TEXTURE_2D, static_cast<GLeglImageOES>(image));
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &texture);
            destroyImage(display, image);
            return 0;
        }
        AHardwareBuffer_acquire(buffer);
        try { images.push_back(DirectImage{buffer, image, texture}); }
        catch (...) { glDeleteTextures(1, &texture); destroyImage(display, image); AHardwareBuffer_release(buffer); return 0; }
        return texture;
    }

    Status frame_direct() noexcept {
        auto* buffer = static_cast<AHardwareBuffer*>(directBuffer);
        if (!buffer) return fail(Errc::NotSupported, "quadro sem AHardwareBuffer para a Surface direta");
        const GLuint texture = direct_texture(buffer);
        if (!texture) return fail(Errc::EncodeFailed, "AHardwareBuffer do export nao virou EGLImage");
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUseProgram(directProgram);
        glUniform1i(directSampler, 0);
        static const GLfloat triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexAttribPointer(static_cast<GLuint>(directPosition), 2, GL_FLOAT, GL_FALSE, 0, triangle);
        glEnableVertexAttribArray(static_cast<GLuint>(directPosition));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) return fail(Errc::EncodeFailed, "GLES falhou ao desenhar o quadro direto (0x%x)", error);
        presentation(display, surface, static_cast<EGLnsecsANDROID>(ptsUs) * 1000);
        if (!eglSwapBuffers(display, surface))
            return fail(Errc::EncodeFailed, "Surface do encoder recusou o quadro direto (0x%x)", eglGetError());
        // O motor reusa o slot (e o AHardwareBuffer) assim que este quadro
        // volta: a leitura do GLES tem de ter terminado (prazo do alimentador).
        glFinish();
        return OkStatus;
    }

    Status frame() noexcept {
        if (simulateHang) std::this_thread::sleep_for(std::chrono::seconds(30));
        if (direct) return frame_direct();
        const u32 w = width, h = height;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texY);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels.data());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texC);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(w / 2), static_cast<GLsizei>(h / 2),
                        GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, pixels.data() + static_cast<usize>(w) * h);
        glUseProgram(program);
        glUniform1i(samplerY, 0);
        glUniform1i(samplerC, 1);
        glUniform4f(matrix, kr, kb, fullRange ? 1.0f : 0.0f, 0.0f);
        static const GLfloat triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexAttribPointer(static_cast<GLuint>(position), 2, GL_FLOAT, GL_FALSE, 0, triangle);
        glEnableVertexAttribArray(static_cast<GLuint>(position));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) return fail(Errc::EncodeFailed, "GLES falhou ao desenhar na Surface (0x%x)", error);
        presentation(display, surface, static_cast<EGLnsecsANDROID>(ptsUs) * 1000);
        if (!eglSwapBuffers(display, surface))
            return fail(Errc::EncodeFailed, "Surface do encoder recusou o quadro (0x%x)", eglGetError());
        return OkStatus;
    }

    void teardown() noexcept {
        if (display != EGL_NO_DISPLAY) {
            if (context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE &&
                eglMakeCurrent(display, surface, surface, context)) {
                if (program) glDeleteProgram(program);
                if (directProgram) glDeleteProgram(directProgram);
                const GLuint textures[2] = {texY, texC};
                glDeleteTextures(2, textures);
                for (const DirectImage& image : images) glDeleteTextures(1, &image.texture);
            }
            for (const DirectImage& image : images)
                if (destroyImage && image.image != EGL_NO_IMAGE_KHR) destroyImage(display, image.image);
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
            eglReleaseThread();
        }
        // A referência própria de cada buffer do anel sai depois do contexto.
        for (const DirectImage& image : images) if (image.buffer) AHardwareBuffer_release(image.buffer);
        images.clear();
        directBuffer = nullptr;
        program = texY = texC = directProgram = 0;
        surface = EGL_NO_SURFACE;
        context = EGL_NO_CONTEXT;
        display = EGL_NO_DISPLAY;
        if (window) ANativeWindow_release(window);
        window = nullptr;
    }

    static void run(std::shared_ptr<SurfaceFeeder> self) noexcept {
        set_current_thread_name("aurea-enc-surface");
        for (;;) {
            Job job = Job::None;
            {
                std::unique_lock lock(self->mutex);
                self->wake.wait(lock, [&] { return self->job != Job::None || self->abandoned; });
                if (self->job == Job::None) {   // abandonado ocioso
                    lock.unlock();
                    self->teardown();
                    return;
                }
                job = self->job;
            }
            Status status = OkStatus;
            if (job == Job::Init) status = self->init();
            else if (job == Job::Frame) status = self->frame();
            else self->teardown();
            bool abandoned = false;
            {
                std::lock_guard lock(self->mutex);
                self->result = status;
                self->job = Job::None;
                self->completed = self->serial;
                abandoned = self->abandoned;
            }
            self->finished.notify_all();
            if (job == Job::Quit) return;
            if (abandoned) {   // voltou de uma travada depois que o export desistiu
                self->teardown();
                return;
            }
        }
    }
};

struct Encoder::Impl {
    AMediaCodec* codec = nullptr;
    Request request{};
    bool started = false, eosSent = false;
    std::vector<u8> csd;
    char detail[256]{};
    char description[200]{};
    // Buffer
    struct Plane { usize offset = 0; i32 col = 1; i32 row = 0; };
    Plane planes[3]{};
    usize extent = 0;
    i32 colorFormat = 0;
    // Surface
    ANativeWindow* window = nullptr;
    std::shared_ptr<SurfaceFeeder> feeder;
    std::thread feederThread;
    bool feederHung = false;
    void* directBuffer = nullptr;      ///< surface-direct: AHardwareBuffer do próximo send (do motor)

    Status fail(Errc code, const char* format, ...) noexcept __attribute__((format(printf, 3, 4))) {
        va_list args;
        va_start(args, format);
        std::vsnprintf(detail, sizeof(detail), format, args);
        va_end(args);
        return Status{code, detail};
    }

    /// Entrega um trabalho ao alimentador e espera com prazo (batendo o
    /// coração do export: é espera com prazo, não travada na plataforma).
    Status call(SurfaceFeeder::Job job, u64 timeoutNs) noexcept {
        if (!feeder || feederHung) return fail(Errc::Timeout, "Surface do encoder travada antes");
        std::unique_lock lock(feeder->mutex);
        feeder->job = job;
        const u64 mine = ++feeder->serial;
        feeder->wake.notify_one();
        const u64 deadline = monotonic_ns() + timeoutNs;
        while (feeder->completed != mine) {
            feeder->finished.wait_for(lock, std::chrono::milliseconds(20));
            if (feeder->completed == mine) break;
            if (request.heartbeat) request.heartbeat->store(monotonic_ns(), std::memory_order_release);
            if (monotonic_ns() > deadline) {
                feederHung = true;
                return fail(Errc::Timeout, "Surface do encoder travou no driver (EGL nao voltou em %llu ms)",
                            static_cast<unsigned long long>(timeoutNs / 1'000'000));
            }
        }
        if (feeder->result.ok()) return OkStatus;
        std::snprintf(detail, sizeof(detail), "%s", feeder->detail);
        return Status{feeder->result.code(), detail};
    }

    Status open_surface() noexcept {
        if (AMediaCodec_createInputSurface(codec, &window) != AMEDIA_OK || !window)
            return fail(Errc::EncoderUnavailable, "encoder %s sem Surface de entrada", description);
        try {
            feeder = std::make_shared<SurfaceFeeder>();
            feeder->window = window;
            ANativeWindow_acquire(window);
            feeder->width = request.width;
            feeder->height = request.height;
            const u8 m = request.color.matrix;
            feeder->kr = m == 5 || m == 6 ? 0.299f : m == 9 || m == 10 ? 0.2627f : 0.2126f;
            feeder->kb = m == 5 || m == 6 ? 0.114f : m == 9 || m == 10 ? 0.0593f : 0.0722f;
            feeder->fullRange = request.color.fullRange;
            feeder->simulateHang = request.simulateHang;
            feeder->direct = request.direct;
            // Direto: o quadro chega no AHardwareBuffer; os planos NV12 não são copiados.
            if (!request.direct) feeder->pixels.resize(static_cast<usize>(request.width) * request.height * 3 / 2);
            feederThread = std::thread(&SurfaceFeeder::run, feeder);
        } catch (...) {
            if (feeder && feeder->window) { ANativeWindow_release(feeder->window); feeder->window = nullptr; }
            feeder.reset();
            return fail(Errc::OutOfMemory, "alimentador da Surface do encoder");
        }
        return call(SurfaceFeeder::Job::Init, 5'000'000'000ull);
    }

    void stop_feeder() noexcept {
        if (!feeder) return;
        if (!feederHung && call(SurfaceFeeder::Job::Quit, 3'000'000'000ull).ok()) {
            if (feederThread.joinable()) feederThread.join();
        } else {
            {
                std::lock_guard lock(feeder->mutex);
                feeder->abandoned = true;
            }
            feeder->wake.notify_all();
            AUREA_LOG_WARN("encoder %s: thread da Surface presa no driver; abandonada", description);
            if (feederThread.joinable()) feederThread.detach();
        }
        feeder.reset();
        feederHung = false;
    }

    Status read_buffer_layout() noexcept {
        using GetInputFormat = AMediaFormat* (*)(AMediaCodec*);
        static const auto getInputFormat = reinterpret_cast<GetInputFormat>(dlsym(RTLD_DEFAULT, "AMediaCodec_getInputFormat"));
        if (!getInputFormat) return fail(Errc::EncoderUnavailable, "sem AMediaCodec_getInputFormat (API < 28)");
        AMediaFormat* input = getInputFormat(codec);
        if (!input) return fail(Errc::EncoderUnavailable, "encoder %s sem formato de entrada", description);
        i32 color = colorFormat, stride = 0, slice = 0;
        (void)AMediaFormat_getInt32(input, "color-format", &color);
        (void)AMediaFormat_getInt32(input, "stride", &stride);
        (void)AMediaFormat_getInt32(input, "slice-height", &slice);
        void* image = nullptr;
        size_t imageBytes = 0;
        bool described = false;
        const u32 w = request.width, h = request.height;
        if (AMediaFormat_getBuffer(input, "image-data", &image, &imageBytes) && image && imageBytes >= 100) {
            // MediaImage (100 bytes) ou MediaImage2 (104): tipo, planos,
            // largura, altura, bits [, bits alocados], 4 × {offset, colInc,
            // rowInc, subamostragem H, V}.
            u32 head[6]{};
            std::memcpy(head, image, sizeof(head));
            const usize planesAt = imageBytes >= 104 ? 24 : 20;
            const bool yuv8 = head[0] == 1 && head[1] == 3 && head[4] == 8 &&
                              (imageBytes < 104 || head[5] == 8);
            if (yuv8 && head[2] >= w && head[3] >= h) {
                bool valid = true;
                for (int p = 0; p < 3; ++p) {
                    i32 info[5]{};
                    std::memcpy(info, static_cast<const u8*>(image) + planesAt + static_cast<usize>(p) * 20, sizeof(info));
                    const i32 sub = p == 0 ? 1 : 2;
                    if (info[0] < 0 || info[1] <= 0 || info[2] <= 0 || info[3] != sub || info[4] != sub) valid = false;
                    planes[p] = Plane{static_cast<usize>(info[0]), info[1], info[2]};
                }
                described = valid;
            }
        }
        if (!described) {
            const usize s = static_cast<usize>(stride > 0 ? stride : static_cast<i32>(w));
            const usize sh = static_cast<usize>(slice > 0 ? slice : static_cast<i32>(h));
            if (s < w || sh < h) {
                AMediaFormat_delete(input);
                return fail(Errc::UnsupportedFormat, "layout de entrada %zux%zu menor que %ux%u", s, sh, w, h);
            }
            if (color == kColorSemiPlanar || color == kColorPackedSemiPlanar) {
                planes[0] = Plane{0, 1, static_cast<i32>(s)};
                planes[1] = Plane{s * sh, 2, static_cast<i32>(s)};
                planes[2] = Plane{s * sh + 1, 2, static_cast<i32>(s)};
            } else if (color == kColorPlanar || color == kColorPackedPlanar) {
                planes[0] = Plane{0, 1, static_cast<i32>(s)};
                planes[1] = Plane{s * sh, 1, static_cast<i32>(s / 2)};
                planes[2] = Plane{s * sh + (s / 2) * (sh / 2), 1, static_cast<i32>(s / 2)};
            } else {
                AMediaFormat_delete(input);
                // Flexível sem image-data (ou formato do fabricante): o layout
                // não é conhecido — nunca presumir.
                return fail(Errc::UnsupportedFormat, "formato de entrada 0x%x sem layout descrito", static_cast<unsigned>(color));
            }
        }
        AMediaFormat_delete(input);
        extent = 0;
        for (int p = 0; p < 3; ++p) {
            const usize cols = p == 0 ? w : w / 2, rows = p == 0 ? h : h / 2;
            extent = std::max(extent, planes[p].offset + (rows - 1) * static_cast<usize>(planes[p].row) +
                                          (cols - 1) * static_cast<usize>(planes[p].col) + 1);
        }
        AUREA_LOG_INFO("encoder buffer %s: formato 0x%x Y(%zu,%d,%d) U(%zu,%d,%d) V(%zu,%d,%d) %s",
                       description, static_cast<unsigned>(color), planes[0].offset, planes[0].col, planes[0].row,
                       planes[1].offset, planes[1].col, planes[1].row, planes[2].offset, planes[2].col, planes[2].row,
                       described ? "image-data" : "stride/slice-height");
        return OkStatus;
    }

    Status open(const Request& r, const CodecInfo& info) noexcept {
        request = r;
        const bool surfaceInput = r.input == Input::Surface;
        if (!r.width || !r.height || (r.width & 1u) || (r.height & 1u) || !std::isfinite(r.fps) || r.fps <= 0.0)
            return fail(Errc::InvalidArgument, "pedido invalido ao encoder do aparelho");
        if (surfaceInput) {
            if (!info.surface) return fail(Errc::NotSupported, "encoder %s nao anuncia entrada por Surface", info.name);
            colorFormat = kColorSurface;
        } else {
            // O formato vem do que o ENCODER anuncia, na ordem do que é mais
            // comum e mais testado: NV12, flexível (layout pelo image-data),
            // I420 e os "packed" equivalentes.
            for (const i32 wanted : {kColorSemiPlanar, kColorFlexible, kColorPlanar, kColorPackedSemiPlanar, kColorPackedPlanar})
                if (std::find(info.colorFormats.begin(), info.colorFormats.end(), wanted) != info.colorFormats.end()) {
                    colorFormat = wanted;
                    break;
                }
            if (!colorFormat) return fail(Errc::UnsupportedFormat, "encoder %s sem formato YUV 4:2:0 de 8 bits", info.name);
        }
        if (surfaceInput) std::snprintf(description, sizeof(description), "%s:%s", r.direct ? "surface-direct" : "surface", info.name);
        else std::snprintf(description, sizeof(description), "buffer:%s:0x%x", info.name, static_cast<unsigned>(colorFormat));
        codec = AMediaCodec_createCodecByName(info.name);
        if (!codec) return fail(Errc::EncoderUnavailable, "MediaCodec nao criou %s", info.name);
        AMediaFormat* format = AMediaFormat_new();
        if (!format) return fail(Errc::OutOfMemory, "MediaFormat do encoder");
        AMediaFormat_setString(format, "mime", r.hevc ? "video/hevc" : "video/avc");
        AMediaFormat_setInt32(format, "width", static_cast<i32>(r.width));
        AMediaFormat_setInt32(format, "height", static_cast<i32>(r.height));
        AMediaFormat_setInt32(format, "color-format", colorFormat);
        AMediaFormat_setInt32(format, "bitrate", static_cast<i32>(std::min<u32>(r.bitrateBps ? r.bitrateBps : 12'000'000u, 0x7fffffffu)));
        // Modo de taxa só quando anunciado; senão o padrão do encoder.
        i32 mode = -1;
        if (r.rateMode == 0) mode = info.cbr ? kBitrateCbr : info.vbr ? kBitrateVbr : -1;
        else mode = info.vbr ? kBitrateVbr : info.cbr ? kBitrateCbr : -1;
        if (mode >= 0) AMediaFormat_setInt32(format, "bitrate-mode", mode);
        const f64 roundedFps = std::round(r.fps);
        if (std::abs(r.fps - roundedFps) < 1e-6) AMediaFormat_setInt32(format, "frame-rate", static_cast<i32>(roundedFps));
        else AMediaFormat_setFloat(format, "frame-rate", static_cast<float>(r.fps));
        const f64 gopSeconds = r.keyframeFrames ? static_cast<f64>(r.keyframeFrames) / r.fps : 2.0;
        const f64 roundedGop = std::round(gopSeconds);
        if (roundedGop >= 1.0 && std::abs(gopSeconds - roundedGop) < 1e-6)
            AMediaFormat_setInt32(format, "i-frame-interval", static_cast<i32>(roundedGop));
        else AMediaFormat_setFloat(format, "i-frame-interval", static_cast<float>(std::max(gopSeconds, 0.001)));
        if (r.baseline && !r.hevc && info.baselineProfile) {
            AMediaFormat_setInt32(format, "profile", info.baselineProfile);
            if (info.baselineLevel) AMediaFormat_setInt32(format, "level", info.baselineLevel);
        }
        if (const i32 standard = color_standard(r.color.matrix)) AMediaFormat_setInt32(format, "color-standard", standard);
        AMediaFormat_setInt32(format, "color-range", r.color.fullRange ? 1 : 2);
        if (const i32 transfer = color_transfer(r.color.transfer)) AMediaFormat_setInt32(format, "color-transfer", transfer);
        // Sem quadros B: o sink exige um pacote por quadro, na ordem.
        AMediaFormat_setInt32(format, "max-bframes", 0);
        const media_status_t configured = AMediaCodec_configure(codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        AMediaFormat_delete(format);
        if (configured != AMEDIA_OK)
            return fail(Errc::EncoderUnavailable, "MediaCodec configure recusou %s (%d)", description, static_cast<int>(configured));
        if (const Status s = surfaceInput ? open_surface() : read_buffer_layout(); !s.ok()) return s;
        if (AMediaCodec_start(codec) != AMEDIA_OK) return fail(Errc::EncoderUnavailable, "MediaCodec nao iniciou %s", description);
        started = true;
        return OkStatus;
    }

    Status send_surface(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 ptsUs, u64 timeoutNs) noexcept {
        if (!feeder || feederHung) return fail(Errc::Timeout, "Surface do encoder travada antes");
        if (request.direct) {
            // surface-direct: o R'G'B' já está no AHardwareBuffer do slot (o
            // motor esperou o fence); os planos NV12 não passam pelo GLES.
            {
                std::lock_guard lock(feeder->mutex);
                feeder->directBuffer = directBuffer;
                feeder->ptsUs = ptsUs;
            }
            directBuffer = nullptr;
            return call(SurfaceFeeder::Job::Frame, timeoutNs);
        }
        {
            // Os planos chegam na memória mapeada do readback do Vulkan: nunca
            // vão direto a outra API (no emulador, gfxstream, o upload GLES a
            // partir dela trava o eglSwapBuffers seguinte para sempre; em
            // aparelho é memória de outro driver). Cópia para a heap do
            // alimentador, que está parado entre um quadro e outro.
            std::lock_guard lock(feeder->mutex);
            const u32 w = request.width, h = request.height;
            u8* dst = feeder->pixels.data();
            for (u32 r = 0; r < h; ++r)
                std::memcpy(dst + static_cast<usize>(r) * w, y + static_cast<usize>(r) * ys, w);
            dst += static_cast<usize>(w) * h;
            for (u32 r = 0; r < h / 2; ++r)
                std::memcpy(dst + static_cast<usize>(r) * w, uv + static_cast<usize>(r) * uvs, w);
            feeder->ptsUs = ptsUs;
        }
        return call(SurfaceFeeder::Job::Frame, timeoutNs);
    }

    Status send_buffer(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 ptsUs) noexcept {
        const ssize_t index = AMediaCodec_dequeueInputBuffer(codec, 2000);
        if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return Errc::Busy;
        if (index < 0) return fail(Errc::EncodeFailed, "dequeueInputBuffer falhou (%zd)", index);
        size_t capacity = 0;
        u8* buffer = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(index), &capacity);
        if (!buffer || capacity < extent) {
            (void)AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(index), 0, 0, ptsUs, 0);
            return fail(Errc::UnsupportedFormat, "buffer de entrada com %zu bytes; o layout pede %zu", capacity, extent);
        }
        const u32 w = request.width, h = request.height;
        const Plane& py = planes[0];
        for (u32 row = 0; row < h; ++row) {
            u8* dst = buffer + py.offset + static_cast<usize>(row) * py.row;
            const u8* src = y + static_cast<usize>(row) * ys;
            if (py.col == 1) std::memcpy(dst, src, w);
            else for (u32 x = 0; x < w; ++x) dst[static_cast<usize>(x) * py.col] = src[x];
        }
        const Plane& pu = planes[1];
        const Plane& pv = planes[2];
        const bool nv12 = pu.col == 2 && pv.col == 2 && pu.row == pv.row && pv.offset == pu.offset + 1;
        for (u32 row = 0; row < h / 2; ++row) {
            const u8* src = uv + static_cast<usize>(row) * uvs;
            if (nv12) {
                std::memcpy(buffer + pu.offset + static_cast<usize>(row) * pu.row, src, w);
                continue;
            }
            u8* du = buffer + pu.offset + static_cast<usize>(row) * pu.row;
            u8* dv = buffer + pv.offset + static_cast<usize>(row) * pv.row;
            for (u32 x = 0; x < w / 2; ++x) {
                du[static_cast<usize>(x) * pu.col] = src[2 * x];
                dv[static_cast<usize>(x) * pv.col] = src[2 * x + 1];
            }
        }
        if (AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(index), 0, capacity, static_cast<u64>(ptsUs), 0) != AMEDIA_OK)
            return fail(Errc::EncodeFailed, "queueInputBuffer recusou o quadro");
        return OkStatus;
    }

    void close() noexcept {
        // Primeiro o produtor (EGL) solta a Surface; depois o encoder.
        stop_feeder();
        AMediaCodec* c = codec;
        ANativeWindow* w = window;
        const bool stop = started;
        codec = nullptr;
        window = nullptr;
        started = false;
        if (!c && !w) return;
        // stop/delete de um encoder do fabricante que travou pode não voltar:
        // numa thread própria, com prazo; passado o prazo ela é abandonada (o
        // export continua no próximo degrau em vez de ficar preso aqui).
        auto done = std::make_shared<std::atomic<bool>>(false);
        auto release = [c, w, stop, done] {
            if (c) {
                if (stop) (void)AMediaCodec_stop(c);
                (void)AMediaCodec_delete(c);
            }
            if (w) ANativeWindow_release(w);
            done->store(true, std::memory_order_release);
        };
        try {
            std::thread worker(release);
            const u64 deadline = monotonic_ns() + 3'000'000'000ull;
            while (!done->load(std::memory_order_acquire) && monotonic_ns() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (done->load(std::memory_order_acquire)) worker.join();
            else {
                AUREA_LOG_WARN("encoder %s: stop nao voltou em 3 s; abandonado", description);
                worker.detach();
            }
        } catch (...) {
            release();
        }
    }
};

Encoder::Encoder() noexcept : impl_(new (std::nothrow) Impl()) {}
Encoder::~Encoder() { close(); }

Status Encoder::open(const Request& request, const CodecInfo& codec) noexcept {
    if (!impl_) return Status{Errc::OutOfMemory, "encoder do aparelho"};
    impl_->close();
    impl_->csd.clear();
    impl_->eosSent = false;
    const Status opened = impl_->open(request, codec);
    if (opened.ok()) return OkStatus;
    // O motivo vive em impl_->detail; fechar pode reescrevê-lo.
    char saved[sizeof(impl_->detail)];
    std::snprintf(saved, sizeof(saved), "%s", opened.detail().empty() ? "encoder do aparelho recusou" : opened.detail().data());
    impl_->close();
    std::snprintf(impl_->detail, sizeof(impl_->detail), "%s", saved);
    return Status{opened.code(), impl_->detail};
}

Status Encoder::send(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 ptsUs, u64 timeoutNs) noexcept {
    if (!impl_ || !impl_->started) return Status{Errc::InvalidState, "encoder do aparelho fechado"};
    if (!y || !uv || ys < impl_->request.width || uvs < impl_->request.width || ptsUs < 0)
        return Status{Errc::InvalidArgument, "quadro invalido para o encoder do aparelho"};
    if (impl_->request.simulateStall) return OkStatus;   // teste: o quadro nunca chega ao encoder
    return impl_->request.input == Input::Surface ? impl_->send_surface(y, ys, uv, uvs, ptsUs, timeoutNs)
                                                  : impl_->send_buffer(y, ys, uv, uvs, ptsUs);
}

Status Encoder::end_of_stream() noexcept {
    if (!impl_ || !impl_->started) return Status{Errc::InvalidState, "encoder do aparelho fechado"};
    if (impl_->eosSent) return OkStatus;
    if (impl_->request.input == Input::Surface) {
        if (AMediaCodec_signalEndOfInputStream(impl_->codec) != AMEDIA_OK)
            return impl_->fail(Errc::EncodeFailed, "signalEndOfInputStream recusado");
    } else {
        const ssize_t index = AMediaCodec_dequeueInputBuffer(impl_->codec, 2000);
        if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return Errc::Busy;
        if (index < 0) return impl_->fail(Errc::EncodeFailed, "dequeueInputBuffer no EOS falhou (%zd)", index);
        if (AMediaCodec_queueInputBuffer(impl_->codec, static_cast<size_t>(index), 0, 0, 0,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != AMEDIA_OK)
            return impl_->fail(Errc::EncodeFailed, "EOS recusado pelo encoder");
    }
    impl_->eosSent = true;
    return OkStatus;
}

Status Encoder::receive(Packet& out, bool& got, bool& eos, i64 timeoutUs) noexcept {
    got = eos = false;
    if (!impl_ || !impl_->started) return Status{Errc::InvalidState, "encoder do aparelho fechado"};
    for (;;) {
        AMediaCodecBufferInfo info{};
        const ssize_t index = AMediaCodec_dequeueOutputBuffer(impl_->codec, &info, timeoutUs);
        if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return Errc::Busy;
        if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            // Encoders que só mandam os parâmetros no formato de saída.
            if (impl_->csd.empty()) {
                if (AMediaFormat* format = AMediaCodec_getOutputFormat(impl_->codec)) {
                    for (const char* key : {"csd-0", "csd-1", "csd-2"}) {
                        void* data = nullptr;
                        size_t bytes = 0;
                        if (AMediaFormat_getBuffer(format, key, &data, &bytes) && data && bytes) {
                            try { impl_->csd.insert(impl_->csd.end(), static_cast<u8*>(data), static_cast<u8*>(data) + bytes); }
                            catch (...) { impl_->csd.clear(); }
                        }
                    }
                    AMediaFormat_delete(format);
                }
            }
            timeoutUs = 0;
            continue;
        }
        if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) { timeoutUs = 0; continue; }
        if (index < 0) return impl_->fail(Errc::EncodeFailed, "dequeueOutputBuffer falhou (%zd)", index);
        size_t capacity = 0;
        u8* buffer = AMediaCodec_getOutputBuffer(impl_->codec, static_cast<size_t>(index), &capacity);
        const u32 flags = info.flags;
        if (info.size > 0 && (!buffer || info.offset < 0 ||
                              static_cast<usize>(info.offset) + static_cast<usize>(info.size) > capacity)) {
            (void)AMediaCodec_releaseOutputBuffer(impl_->codec, static_cast<size_t>(index), false);
            return impl_->fail(Errc::EncodeFailed, "pacote do encoder fora do buffer");
        }
        const u8* data = info.size > 0 ? buffer + info.offset : nullptr;
        bool copied = true;
        if ((flags & kFlagConfig) && data) {
            try { impl_->csd.assign(data, data + info.size); } catch (...) { copied = false; }
        } else if (data) {
            try { out.data.assign(data, data + info.size); } catch (...) { copied = false; }
            out.ptsUs = info.presentationTimeUs;
            out.key = (flags & kFlagKey) != 0;
            got = copied;
        }
        (void)AMediaCodec_releaseOutputBuffer(impl_->codec, static_cast<size_t>(index), false);
        if (!copied) return impl_->fail(Errc::OutOfMemory, "copiar pacote do encoder");
        if (flags & kFlagEos) eos = true;
        if (got || eos) return OkStatus;
        timeoutUs = 0;
    }
}

const std::vector<u8>& Encoder::codec_config() const noexcept {
    static const std::vector<u8> empty;
    return impl_ ? impl_->csd : empty;
}

u32 Encoder::max_in_flight() const noexcept {
    return impl_ && impl_->request.input == Input::Surface ? kSurfaceInFlight : 0u;
}

const char* Encoder::describe() const noexcept { return impl_ ? impl_->description : ""; }

void Encoder::set_direct_buffer(void* buffer) noexcept { if (impl_) impl_->directBuffer = buffer; }

void Encoder::close() noexcept { if (impl_) impl_->close(); }

} // namespace aurea::android_video

#endif
