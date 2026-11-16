// =============================================================================
//  Aurea / platform / desktop / DesktopVideoTest.cpp
//
//  Teste sem janela da prévia do Aurea PC com um projeto de verdade. NÃO vai
//  no app. Como o DesktopSmoke: JVM de verdade, aurea.dll carregada como a UI
//  carrega, as MESMAS exportações (tabela de codecs com decode de hardware,
//  apresentação no host em 1280x720). Abre uma CÓPIA do projeto e mede:
//    - parado (seek ao quadro 0 e a outros): o quadro entregue à UI tem o
//      vídeo (centro não preto) e quanto demora;
//    - tocando 3 s: quadros pretos, repetidos e o maior intervalo;
//    - a captura do projeto (miniatura da Home) tocando e parada: tempo e
//      resultado (antes: "prazo da captura esgotado").
//  O áudio do motor sai em silêncio (AUREA_AUDIO_SILENT=1): nada toca.
//
//  Uso: aurea_pc_video_test.exe <projeto.aurea> [pasta de saída]
//  Rode escondido, do PowerShell, com engine\build\pc-jni\bin no PATH.
// =============================================================================
#include <jni.h>
#include <windows.h>

#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
HMODULE g_aurea = nullptr;
int g_failures = 0;

template <typename Fn>
Fn sym(const char* name) {
    auto p = reinterpret_cast<Fn>(GetProcAddress(g_aurea, name));
    if (!p) {
        std::printf("FALHA: exportacao ausente %s\n", name);
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 2);
    }
    return p;
}

#define ENGINE(name) "Java_com_aurea_aurea_engine_AureaEngine_" #name
#define FRAMES(name) "Java_com_aurea_aurea_desktop_DesktopFrames_" #name

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "  ok  " : "FALHA ", what);
    if (!ok) ++g_failures;
}

double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

void write_bmp(const std::filesystem::path& path, const unsigned char* bgra, int w, int h, int rowBytes) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + static_cast<DWORD>(w) * h * 4;
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = -h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    std::fwrite(&fh, sizeof(fh), 1, f);
    std::fwrite(&ih, sizeof(ih), 1, f);
    for (int y = 0; y < h; ++y) std::fwrite(bgra + static_cast<size_t>(y) * rowBytes, 1, static_cast<size_t>(w) * 4, f);
    std::fclose(f);
}

// Um quadro entregue: brilho médio do miolo do palco e uma assinatura (repetido?).
struct Sample { double atMs = 0; long long serial = 0; double luma = 0; unsigned long long sig = 0; };

Sample measure(const unsigned char* px, int w, int h, int rowBytes) {
    Sample s;
    double sum = 0;
    int n = 0;
    unsigned long long sig = 1469598103934665603ull;
    // Miolo de 40% x 40%: o palco 16:9 ocupa o centro da área de 1280x720.
    for (int y = h * 3 / 10; y < h * 7 / 10; y += 4)
        for (int x = w * 3 / 10; x < w * 7 / 10; x += 4) {
            const unsigned char* p = px + static_cast<size_t>(y) * rowBytes + static_cast<size_t>(x) * 4;
            sum += 0.0722 * p[0] + 0.7152 * p[1] + 0.2126 * p[2];
            ++n;
            sig = (sig ^ (p[0] | (p[1] << 8) | (p[2] << 16))) * 1099511628211ull;
        }
    s.luma = n ? sum / n : 0;
    s.sig = sig;
    return s;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    // Nada toca nas caixas: a saída de áudio do motor escreve zeros.
    SetEnvironmentVariableA("AUREA_AUDIO_SILENT", "1");
    if (argc < 2) {
        std::printf("uso: aurea_pc_video_test <projeto.aurea> [pasta de saida]\n");
        return 2;
    }
    const std::filesystem::path original = argv[1];
    const std::filesystem::path outDir = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::current_path();

    // Cópia do projeto (e dos arquivos ao lado dele): o original nunca é aberto pelo motor.
    const std::filesystem::path work = std::filesystem::temp_directory_path() / L"aurea-pc-video-test";
    std::error_code ec;
    std::filesystem::remove_all(work / L"proj", ec);
    for (const wchar_t* d : {L"cache", L"docs", L"plugins", L"proj"}) std::filesystem::create_directories(work / d);
    const std::filesystem::path project = work / L"proj" / original.filename();
    for (const auto& e : std::filesystem::directory_iterator(original.parent_path())) {
        const std::wstring name = e.path().filename().wstring();
        if (e.is_regular_file() && name.rfind(original.filename().wstring(), 0) == 0)
            std::filesystem::copy_file(e.path(), work / L"proj" / name, std::filesystem::copy_options::overwrite_existing);
    }

    // --- JVM e aurea.dll (como o DesktopSmoke) ------------------------------------
    const std::filesystem::path jvmPath = AUREA_SMOKE_JVM;
    SetDllDirectoryW(jvmPath.parent_path().parent_path().c_str());
    HMODULE jvmDll = LoadLibraryW(jvmPath.c_str());
    if (!jvmDll) { std::printf("FALHA: jvm.dll nao carregou (%lu)\n", GetLastError()); return 2; }
    SetDllDirectoryW(nullptr);
    using CreateVm = jint(JNICALL*)(JavaVM**, void**, void*);
    auto createVm = reinterpret_cast<CreateVm>(GetProcAddress(jvmDll, "JNI_CreateJavaVM"));
    JavaVMOption opts[1]{};
    opts[0].optionString = const_cast<char*>("-Xrs");
    JavaVMInitArgs args{};
    args.version = JNI_VERSION_1_8;
    args.nOptions = 1;
    args.options = opts;
    args.ignoreUnrecognized = JNI_TRUE;
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    if (!createVm || createVm(&vm, reinterpret_cast<void**>(&env), &args) != JNI_OK) { std::printf("FALHA: JVM\n"); return 2; }
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_aurea = LoadLibraryW((std::filesystem::path(exe).parent_path() / L"aurea.dll").c_str());
    if (!g_aurea) { std::printf("FALHA: aurea.dll nao carregou (%lu)\n", GetLastError()); return 2; }
    check(sym<jint(JNICALL*)(JavaVM*, void*)>("JNI_OnLoad")(vm, nullptr) >= JNI_VERSION_1_6, "JNI_OnLoad");
    if (env->ExceptionCheck()) env->ExceptionClear();

    auto nativeCreate = sym<jlong(JNICALL*)(JNIEnv*, jclass)>(ENGINE(nativeCreate));
    auto nativeInitialize = sym<jboolean(JNICALL*)(JNIEnv*, jclass, jlong, jfloat, jstring, jstring, jboolean, jlongArray,
                                                   jintArray, jstring)>(ENGINE(nativeInitialize));
    auto nativeStartupError = sym<jstring(JNICALL*)(JNIEnv*, jclass, jlong)>(ENGINE(nativeStartupError));
    auto nativeLoadProject = sym<jint(JNICALL*)(JNIEnv*, jclass, jlong, jstring)>(ENGINE(nativeLoadProject));
    auto nativeAttachSurface = sym<jboolean(JNICALL*)(JNIEnv*, jclass, jlong, jobject, jint, jint)>(ENGINE(nativeAttachSurface));
    auto nativeSubmitCommands = sym<jint(JNICALL*)(JNIEnv*, jclass, jlong, jobject, jint, jobject, jint)>(ENGINE(nativeSubmitCommands));
    auto nativeCaptureFrame = sym<jint(JNICALL*)(JNIEnv*, jclass, jlong, jint, jobject, jintArray)>(ENGINE(nativeCaptureFrame));
    auto nativeCapturePreviewFrame = sym<jint(JNICALL*)(JNIEnv*, jclass, jlong, jint, jobject, jintArray)>(ENGINE(nativeCapturePreviewFrame));
    auto nativePlaybackReport = sym<jstring(JNICALL*)(JNIEnv*, jclass, jlong)>(ENGINE(nativePlaybackReport));
    auto nativeDetachSurface = sym<void(JNICALL*)(JNIEnv*, jclass, jlong)>(ENGINE(nativeDetachSurface));
    auto nativeShutdown = sym<void(JNICALL*)(JNIEnv*, jclass, jlong)>(ENGINE(nativeShutdown));
    auto acquireFrame = sym<jlong(JNICALL*)(JNIEnv*, jclass, jlong, jobject, jint, jintArray)>(FRAMES(nativeAcquireFrame));

    const auto jstr = [&](const std::wstring& s) {
        std::string u(static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, u.data(), static_cast<int>(u.size()), nullptr, nullptr);
        return env->NewStringUTF(u.c_str());
    };
    const auto str = [&](jstring s) {
        if (!s) return std::string();
        const char* c = env->GetStringUTFChars(s, nullptr);
        std::string r = c ? c : "";
        if (c) env->ReleaseStringUTFChars(s, c);
        return r;
    };

    // --- Motor: como a UI do PC (Media.kt: decoders de hardware na tabela) ---------
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    float refresh = 60.0f;
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) refresh = static_cast<float>(mode.dmDisplayFrequency);
    const jlong h = nativeCreate(env, nullptr);
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    const jlong probe[2] = {static_cast<jlong>(mem.ullTotalPhys), static_cast<jlong>(mem.ullAvailPhys)};
    jlongArray probeArray = env->NewLongArray(2);
    env->SetLongArrayRegion(probeArray, 0, 2, probe);
    // tag, encoder, hardware, largura, altura, bits, instâncias (DeviceProfile.kt).
    const jint codecRows[] = {
        0x61766331, 0, 1, 8192, 8192, 8, 16,    // avc1
        0x68766331, 0, 1, 8192, 8192, 10, 16,   // hvc1
        0x76703039, 0, 1, 8192, 8192, 10, 16,   // vp09
        0x61763031, 0, 1, 8192, 8192, 10, 16,   // av01
        0x61766331, 1, 0, 8192, 8192, 8, 16,    // encoder avc1 (FFmpeg)
        0, 0, 0, 0, 0, 0, 0,
    };
    jintArray codecArray = env->NewIntArray(static_cast<jsize>(std::size(codecRows)));
    env->SetIntArrayRegion(codecArray, 0, static_cast<jsize>(std::size(codecRows)), codecRows);
    const bool initialized = nativeInitialize(env, nullptr, h, refresh, jstr((work / L"cache").wstring()),
        jstr((work / L"docs").wstring()), JNI_FALSE, probeArray, codecArray, jstr((work / L"plugins").wstring())) == JNI_TRUE;
    check(initialized, "nativeInitialize");
    if (!initialized) { std::printf("       erro: %s\n", str(nativeStartupError(env, nullptr, h)).c_str()); return 1; }
    const jint loaded = nativeLoadProject(env, nullptr, h, jstr(project.wstring()));
    std::printf("       projeto: %ls (resultado %d)\n", project.filename().c_str(), loaded);
    check(loaded == 0, "nativeLoadProject");

    jclass objectClass = env->FindClass("java/lang/Object");
    jobject dummySurface = objectClass ? env->AllocObject(objectClass) : nullptr;
    check(nativeAttachSurface(env, nullptr, h, dummySurface, 1280, 720) == JNI_TRUE, "nativeAttachSurface 1280x720");

    std::vector<unsigned char> pixels(static_cast<size_t>(3840) * 2160 * 4);
    jobject buffer = env->NewDirectByteBuffer(pixels.data(), static_cast<jlong>(pixels.size()));
    jintArray info = env->NewIntArray(3);
    jint dims[3]{};

    // Bomba de quadros a ~1 ms; guarda cada quadro novo medido.
    const auto pump = [&](double seconds) {
        std::vector<Sample> got;
        const double start = now_ms();
        while (now_ms() - start < seconds * 1000.0) {
            const jlong serial = acquireFrame(env, nullptr, h, buffer, static_cast<jint>(pixels.size()), info);
            if (serial) {
                env->GetIntArrayRegion(info, 0, 3, dims);
                Sample s = measure(pixels.data(), dims[0], dims[1], dims[2]);
                s.atMs = now_ms() - start;
                s.serial = serial;
                got.push_back(s);
            }
            Sleep(1);
        }
        return got;
    };
    const auto submit = [&](aurea::Command& cmd) {
        jobject b = env->NewDirectByteBuffer(&cmd, sizeof(cmd));
        const jint n = nativeSubmitCommands(env, nullptr, h, b, 1, nullptr, 0);
        env->DeleteLocalRef(b);
        return n == 1;
    };
    const auto seek = [&](long long frame, double fps) {
        aurea::Command c;
        c.type = aurea::CommandType::PlaybackSeek;
        c.seek.time.value = static_cast<aurea::i64>(static_cast<double>(frame) * 1e9 / fps);
        return submit(c);
    };
    const auto simple = [&](aurea::CommandType type) {
        aurea::Command c;
        c.type = type;
        return submit(c);
    };
    constexpr double kBlack = 6.0;   // brilho médio do miolo abaixo disto = preto
    int stillBlack = 0;
    const auto still = [&](const char* label, long long frame) {
        check(seek(frame, 30.0), "PlaybackSeek");
        const auto got = pump(2.0);
        int black = 0;
        double firstLit = -1;
        for (const Sample& s : got) {
            if (s.luma < kBlack) ++black;
            else if (firstLit < 0) firstLit = s.atMs;
        }
        const double last = got.empty() ? -1 : got.back().luma;
        std::printf("       %s: %zu quadros em 2 s, pretos=%d, primeiro com video em %.0f ms, brilho final %.1f\n",
                    label, got.size(), black, firstLit, last);
        if (got.empty() || last < kBlack) ++stillBlack;
        return got;
    };
    const auto capture = [&](const char* label, bool preview) {
        std::vector<unsigned char> out(512 * 512 * 4);
        jobject b = env->NewDirectByteBuffer(out.data(), static_cast<jlong>(out.size()));
        jintArray size = env->NewIntArray(2);
        const double t0 = now_ms();
        const jint bytes = preview ? nativeCapturePreviewFrame(env, nullptr, h, 512, b, size)
                                   : nativeCaptureFrame(env, nullptr, h, 512, b, size);
        const double ms = now_ms() - t0;
        jint wh[2]{};
        env->GetIntArrayRegion(size, 0, 2, wh);
        double luma = 0;
        if (bytes > 0) {
            int n = 0;
            for (int y = wh[1] / 4; y < wh[1] * 3 / 4; y += 2)
                for (int x = wh[0] / 4; x < wh[0] * 3 / 4; x += 2) {
                    const unsigned char* p = out.data() + (static_cast<size_t>(y) * wh[0] + x) * 4;
                    luma += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
                    ++n;
                }
            luma = n ? luma / n : 0;
        }
        std::printf("       captura %s: %d bytes %dx%d em %.0f ms, brilho %.1f\n", label, bytes, wh[0], wh[1], ms, luma);
        env->DeleteLocalRef(b);
        env->DeleteLocalRef(size);
        return bytes > 0 && ms < 2000.0 && luma >= kBlack;
    };

    // 1) Abertura: o primeiro quadro (o playhead salvo).
    {
        const auto got = pump(3.0);
        int black = 0;
        for (const Sample& s : got) black += s.luma < kBlack;
        std::printf("       abertura: %zu quadros em 3 s, pretos=%d, brilho final %.1f\n", got.size(), black,
                    got.empty() ? -1.0 : got.back().luma);
        if (!got.empty()) write_bmp(outDir / L"video_abertura.bmp", pixels.data(), dims[0], dims[1], dims[2]);
    }
    // As camadas: o trecho da primeira camada de vídeo diz onde parar.
    long long videoStart = 0, videoEnd = 30;
    {
        auto nativeQueryLayers = sym<jint(JNICALL*)(JNIEnv*, jclass, jlong, jobject, jint, jobject, jint)>(ENGINE(nativeQueryLayers));
        std::vector<aurea::bridge::LayerRow> rows(64);
        std::vector<char> names(8192);
        jobject rb = env->NewDirectByteBuffer(rows.data(), static_cast<jlong>(rows.size() * sizeof(rows[0])));
        jobject nb = env->NewDirectByteBuffer(names.data(), static_cast<jlong>(names.size()));
        const jint n = nativeQueryLayers(env, nullptr, h, rb, static_cast<jint>(rows.size()), nb, static_cast<jint>(names.size()));
        bool found = false;
        for (jint i = 0; i < n; ++i) {
            const auto& r = rows[i];
            const std::string name(names.data() + std::min<size_t>(r.nameOffset, names.size()),
                                   std::min<size_t>(r.nameLength, names.size() - std::min<size_t>(r.nameOffset, names.size())));
            std::printf("       camada %d: tipo %u, quadros %d..%d, deslocamento %d, efeitos %u, \"%s\"\n", i, r.kind,
                        r.startFrame, r.endFrame, r.offsetFrames, r.effectCount, name.c_str());
            if (!found && r.kind == 1) { found = true; videoStart = r.startFrame; videoEnd = r.endFrame; }
        }
        check(found, "projeto com camada de video");
    }
    const long long span = std::max<long long>(1, videoEnd - videoStart);

    // 2) Parado: começo, meio e fim do trecho do vídeo.
    still("parado no inicio do video", videoStart);
    write_bmp(outDir / L"video_parado0.bmp", pixels.data(), dims[0], dims[1], dims[2]);
    still("parado no meio do video", videoStart + span / 2);
    still("parado perto do fim do video", videoStart + std::max<long long>(0, span - 2));
    still("parado no inicio (de novo)", videoStart);
    check(stillBlack == 0, "parado: o quadro final tem o video (nao preto)");
    check(capture("parada (exata)", false), "captura parada sem prazo esgotado");

    // 3) Tocando 3 s, em laço (o trecho do vídeo pode ser curto).
    {
        aurea::Command loop;
        loop.type = aurea::CommandType::PlaybackSetLoop;
        loop.loop.loop = true;
        check(submit(loop), "PlaybackSetLoop");
    }
    check(simple(aurea::CommandType::PlaybackPlay), "PlaybackPlay");
    {
        const auto got = pump(3.0);
        int black = 0, repeated = 0;
        double maxGap = 0;
        for (size_t i = 0; i < got.size(); ++i) {
            black += got[i].luma < kBlack;
            if (i) {
                repeated += got[i].sig == got[i - 1].sig;
                maxGap = std::max(maxGap, got[i].atMs - got[i - 1].atMs);
            }
        }
        std::printf("       tocando: %zu quadros em 3 s (%.1f/s), pretos=%d, repetidos=%d, maior intervalo %.1f ms\n",
                    got.size(), got.size() / 3.0, black, repeated, maxGap);
        check(black == 0, "tocando: nenhum quadro preto");
    }
    check(capture("tocando (previa)", true), "captura tocando sem prazo esgotado");
    {
        const auto got = pump(1.0);
        int black = 0;
        for (const Sample& s : got) black += s.luma < kBlack;
        std::printf("       depois da captura tocando: %zu quadros em 1 s, pretos=%d\n", got.size(), black);
        check(!got.empty() && black == 0, "a previa volta depois da captura");
    }
    check(simple(aurea::CommandType::PlaybackPause), "PlaybackPause");
    {
        const auto got = pump(1.5);
        const double last = got.empty() ? -1 : got.back().luma;
        std::printf("       pausado: brilho final %.1f\n", last);
        write_bmp(outDir / L"video_pausado.bmp", pixels.data(), dims[0], dims[1], dims[2]);
        check(last < 0 || last >= kBlack, "pausado: continua com o video");
    }
    check(capture("pausada (exata)", false), "captura pausada sem prazo esgotado");
    {
        std::string report = str(nativePlaybackReport(env, nullptr, h));
        std::replace(report.begin(), report.end(), '\n', ' ');
        std::printf("       relatorio: %s\n", report.c_str());
    }

    nativeDetachSurface(env, nullptr, h);
    nativeShutdown(env, nullptr, h);
    std::printf("%s (%d falhas)\n", g_failures ? "VIDEO FALHOU" : "VIDEO OK", g_failures);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), g_failures ? 1 : 0);
    return g_failures ? 1 : 0;
}
