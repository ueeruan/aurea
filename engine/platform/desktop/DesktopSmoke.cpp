// =============================================================================
//  Aurea / platform / desktop / DesktopSmoke.cpp
//
//  Teste de fumaça da aurea.dll (Aurea PC). NÃO vai no app.
//
//  Sobe uma JVM de verdade (jvm.dll do JDK), carrega a aurea.dll como o
//  System.loadLibrary faria (JNI_OnLoad) e chama as MESMAS exportações que a
//  interface Compose Desktop chama: cria o motor, abre um projeto com uma
//  forma e um texto, anexa a apresentação no host em 1280x720 e lê os quadros
//  por DesktopFrames.nativeAcquireFrame. Mede:
//    - pixels do primeiro quadro (não nulos, palco ≠ fundo da mesa);
//    - ritmo tocando (deve ficar perto de 60 quadros/s);
//    - custo parado (quadros novos ≈ 0 e CPU do processo ≈ 0);
//    - (a) saída de áudio do PC (make_audio_output): 5 s livre + 5 s com a CPU
//      cheia, buracos (fila do dispositivo vazia tocando) e latência;
//    - (b) ritmo da apresentação no host: intervalo entre quadros prontos
//      (média, p95, máximo) e, a cada vblank do monitor, quadro novo ou
//      repetido — em composições de 60 e de 30 quadros/s;
//    - (c) decode do Media Foundation: quadros/s em arquivos reais.
//
//  Uso: aurea_pc_smoke.exe [pasta de saída] [vídeos...]   (grava smoke_frame.bmp)
//  Sem vídeos na linha: build/playback-matrix/h264-1080p60.mp4 e hevc-1080p30.mp4.
//  AUREA_SMOKE_ONLY=abc escolhe as partes novas (a, b, c); vazio = todas.
//  Rode do PowerShell com a pasta bin do FFmpeg no PATH (ou com as DLLs ao lado).
// =============================================================================
#include <jni.h>
#include <windows.h>

#include "aurea/command/Command.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/media/VideoSource.hpp"
#include "aurea/project/Asset.hpp"
#include "WindowsMedia.hpp"

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "dxgi.lib")

namespace {

using Clock = std::chrono::steady_clock;

HMODULE g_aurea = nullptr;
int g_failures = 0;

template <typename Fn>
Fn sym(const char* name) {
    auto p = reinterpret_cast<Fn>(GetProcAddress(g_aurea, name));
    if (!p) {
        std::printf("FALHA: exportacao ausente %s\n", name);
        std::exit(2);
    }
    return p;
}

#define ENGINE(name) "Java_com_aurea_aurea_engine_AureaEngine_" #name
#define FRAMES(name) "Java_com_aurea_aurea_desktop_DesktopFrames_" #name

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "  ok  " : "FALHA ", what);
    if (!ok) ++g_failures;
}

double process_cpu_seconds() {
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    const auto v = [](FILETIME f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return static_cast<double>(v(k) + v(u)) * 1e-7;
}

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
    ih.biHeight = -h;   // de cima para baixo
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    std::fwrite(&fh, sizeof(fh), 1, f);
    std::fwrite(&ih, sizeof(ih), 1, f);
    for (int y = 0; y < h; ++y) std::fwrite(bgra + static_cast<size_t>(y) * rowBytes, 1, static_cast<size_t>(w) * 4, f);
    std::fclose(f);
}


bool part(char c) {
    const char* only = std::getenv("AUREA_SMOKE_ONLY");
    return !only || !*only || std::strchr(only, c) != nullptr;
}

struct Spread { double mean = 0, p95 = 0, max = 0, sd = 0; };
Spread spread(std::vector<double> v) {
    Spread r;
    if (v.empty()) return r;
    double sum = 0;
    for (const double x : v) sum += x;
    r.mean = sum / v.size();
    double var = 0;
    for (const double x : v) var += (x - r.mean) * (x - r.mean);
    r.sd = std::sqrt(var / v.size());
    std::sort(v.begin(), v.end());
    r.p95 = v[std::min(v.size() - 1, static_cast<size_t>(v.size() * 0.95))];
    r.max = v.back();
    return r;
}

double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

// Espera curta de verdade (timer de alta resolução), sem mexer no
// timeBeginPeriod do processo: o motor tem de funcionar sem ajuda do teste.
void short_wait_us(HANDLE timer, long long us) {
    LARGE_INTEGER due{};
    due.QuadPart = -us * 10;
    if (timer && SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0)) WaitForSingleObject(timer, 100);
    else Sleep(0);
}

// O monitor principal pelo DXGI: a "UI" do teste acorda no vblank dele.
Microsoft::WRL::ComPtr<IDXGIOutput> primary_output() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return {};
    const HMONITOR primary = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        Microsoft::WRL::ComPtr<IDXGIOutput> out;
        for (UINT o = 0; adapter->EnumOutputs(o, &out) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC d{};
            if (SUCCEEDED(out->GetDesc(&d)) && d.Monitor == primary) return out;
        }
    }
    return {};
}

// --- (a) áudio -------------------------------------------------------------------
struct Tone { std::atomic<long long> written{0}; std::atomic<long long> calls{0}; std::atomic<unsigned> maxChunk{0}; double phase = 0; };
void tone_cb(void* ctx, float* out, aurea::u32 frames) {
    auto* t = static_cast<Tone*>(ctx);
    for (aurea::u32 i = 0; i < frames; ++i) {
        // Silêncio: o teste mede fila e relógio, nunca toca som nas caixas.
        const float v = static_cast<float>(0.0 * std::sin(t->phase));
        t->phase += 2.0 * 3.14159265358979 * 440.0 / 48000.0;
        if (t->phase > 6.283185307179586) t->phase -= 6.283185307179586;
        out[i * 2] = out[i * 2 + 1] = v;
    }
    t->written.fetch_add(frames);
    t->calls.fetch_add(1);
    unsigned m = t->maxChunk.load();
    while (frames > m && !t->maxChunk.compare_exchange_weak(m, frames)) {}
}

struct AudioRun { long long underruns = 0; double queueMeanMs = 0, queueMinMs = 1e9; double rate = 0; bool presentedOk = false; bool monotonic = true; };
AudioRun audio_run(aurea::audio::AudioOutput& out, Tone& tone, double seconds, int stressThreads) {
    AudioRun r;
    std::atomic<bool> stop{false};
    std::vector<std::thread> stress;
    for (int i = 0; i < stressThreads; ++i) stress.emplace_back([&] {
        // Carga parecida com decode + render: CPU cheia e memória andando.
        std::vector<unsigned char> mem(32u << 20);
        size_t at = 0;
        while (!stop.load(std::memory_order_relaxed)) { mem[at] += 1; at = (at + 4093) % mem.size(); }
    });
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const double start = now_ms();
    long long lastPresented = -1, firstPresented = -1;
    double firstMs = 0, lastMs = 0;
    bool empty = false;
    double queueSum = 0;
    long long samples = 0;
    while (now_ms() - start < seconds * 1000.0) {
        short_wait_us(timer, 500);
        aurea::i64 presented = 0;
        if (!out.presented(aurea::monotonic_ns(), presented)) continue;
        r.presentedOk = true;
        const double t = now_ms();
        if (lastPresented >= 0 && presented < lastPresented) r.monotonic = false;
        if (firstPresented < 0 && presented > 0) { firstPresented = presented; firstMs = t; }
        lastPresented = presented; lastMs = t;
        const long long queued = tone.written.load() - presented;
        const double qms = queued / 48.0;
        if (t - start > 200.0) {   // o começo do stream não conta
            queueSum += qms; ++samples;
            r.queueMinMs = std::min(r.queueMinMs, qms);
            const bool nowEmpty = queued <= 0;
            if (nowEmpty && !empty) ++r.underruns;
            empty = nowEmpty;
        }
    }
    stop = true;
    for (auto& t : stress) t.join();
    if (timer) CloseHandle(timer);
    r.queueMeanMs = samples ? queueSum / samples : 0;
    if (lastMs > firstMs) r.rate = (lastPresented - firstPresented) / ((lastMs - firstMs) / 1000.0);
    return r;
}

void audio_part() {
    std::printf("-- (a) audio do PC\n");
    auto out = aurea::windows::make_audio_output();
    Tone tone;
    const aurea::Status opened = out->open(&tone_cb, &tone);
    check(opened.ok(), "audio: open");
    if (!opened.ok()) return;
    check(out->start().ok(), "audio: start");
    const int cores = static_cast<int>(std::thread::hardware_concurrency());
    const AudioRun idle = audio_run(*out, tone, 5.0, 0);
    std::printf("       livre: buracos=%lld fila media=%.1f ms minima=%.1f ms taxa=%.0f Hz latencia declarada=%u quadros (%.1f ms) maior pedido=%u quadros\n",
                idle.underruns, idle.queueMeanMs, idle.queueMinMs, idle.rate, out->latency_frames(),
                out->latency_frames() / 48.0, tone.maxChunk.load());
    const AudioRun busy = audio_run(*out, tone, 5.0, cores + 2);
    std::printf("       CPU cheia (%d threads): buracos=%lld fila media=%.1f ms minima=%.1f ms taxa=%.0f Hz\n",
                cores + 2, busy.underruns, busy.queueMeanMs, busy.queueMinMs, busy.rate);
    aurea::windows::AudioOutputReport rep;
    if (aurea::windows::audio_output_report(out.get(), rep))
        std::printf("       saida: %s, buffer %u quadros, latencia do stream %u quadros, buracos do dispositivo (padding 0) %llu, reaberturas %llu\n",
                    rep.backend, rep.bufferFrames, rep.streamLatencyFrames, static_cast<unsigned long long>(rep.glitches),
                    static_cast<unsigned long long>(rep.reopens));
    check(idle.presentedOk && idle.monotonic && busy.monotonic, "audio: posicao tocada disponivel e crescente");
    check(std::abs(idle.rate - 48000.0) < 480.0, "audio: posicao anda a 48 kHz");
    check(idle.underruns == 0, "audio: sem buraco livre");
    check(busy.underruns == 0, "audio: sem buraco com a CPU cheia");
    // Parar e voltar: a posição continua de onde estava (não volta a zero).
    aurea::i64 before = 0, after = 0;
    (void)out->presented(aurea::monotonic_ns(), before);
    out->stop();
    check(out->start().ok(), "audio: start de novo");
    Sleep(300);
    const bool again = out->presented(aurea::monotonic_ns(), after);
    check(again && after >= before, "audio: posicao continua depois de stop/start");
    out->close();
}

// --- (c) decode --------------------------------------------------------------------
void decode_part(const std::vector<std::filesystem::path>& files) {
    std::printf("-- (c) decode do Media Foundation\n");
    aurea::windows::initialize_media();
    aurea::windows::MediaFactory factory;
    for (const auto& file : files) {
        if (!std::filesystem::exists(file)) { std::printf("       (sem %ls)\n", file.c_str()); continue; }
        aurea::Asset asset;
        asset.sourcePath = aurea::windows::utf8(file.wstring());
        const double t0 = now_ms();
        auto dec = factory.open_video(asset, aurea::MediaPriority::Preview);
        const double openMs = now_ms() - t0;
        if (!dec) { std::printf("FALHA  decode: nao abriu %ls\n", file.c_str()); ++g_failures; continue; }
        const auto& info = dec->info();
        int frames = 0;
        aurea::PixelFormat format = aurea::PixelFormat::Unknown;
        unsigned w = 0, h = 0, vw = 0, vh = 0;
        bool eos = false;
        const double cpu0 = process_cpu_seconds();
        const double start = now_ms();
        while (!eos && frames < 600 && now_ms() - start < 15000.0) {
            aurea::FrameRef f;
            aurea::i64 pts = 0;
            if (!dec->next_frame(0, f, pts, eos).ok()) { ++g_failures; std::printf("FALHA  decode: erro no quadro %d\n", frames); break; }
            if (f) {
                ++frames; format = f->format; w = f->width; h = f->height; vw = f->visibleWidth; vh = f->visibleHeight;
                // AUREA_SMOKE_DUMP=pasta: o 1º quadro YUV (parte visível, planos
                // compactos) para comparar com o decode do FFmpeg.
                const char* dump = std::getenv("AUREA_SMOKE_DUMP");
                if (frames == 1 && dump && *dump && f->planeCount == 2) {
                    const unsigned bpp = f->format == aurea::PixelFormat::P010 ? 2 : 1;
                    const auto path = std::filesystem::path(dump) / (file.stem().wstring() + L".yuv");
                    if (FILE* out = _wfopen(path.c_str(), L"wb")) {
                        for (unsigned y = 0; y < f->visibleHeight; ++y)
                            std::fwrite(f->planes[0] + static_cast<size_t>(y + f->cropTop) * f->strides[0] + static_cast<size_t>(f->cropLeft) * bpp, 1, static_cast<size_t>(f->visibleWidth) * bpp, out);
                        for (unsigned y = 0; y < (f->visibleHeight + 1) / 2; ++y)
                            std::fwrite(f->planes[1] + static_cast<size_t>(y + f->cropTop / 2) * f->strides[1] + static_cast<size_t>(f->cropLeft) * bpp, 1, static_cast<size_t>(f->visibleWidth) * bpp, out);
                        std::fclose(out);
                    }
                }
            }
        }
        const double secs = (now_ms() - start) / 1000.0;
        const double cpu = process_cpu_seconds() - cpu0;
        static const char* kFormats[] = {"?", "RGBA8", "RGBA16F", "NV12", "P010", "NV21", "YUV420P", "Opaque"};
        std::printf("       %ls: %s %ux%u %.0f fps | %s | hw=%d | quadro %s %ux%u (visivel %ux%u) | %d quadros em %.2f s = %.1f fps | CPU %.0f%% de um nucleo | abrir %.0f ms\n",
                    file.filename().c_str(), info.codec, info.codedWidth, info.codedHeight, info.fps, info.decoderName,
                    info.hardwareDecoder ? 1 : 0, kFormats[std::min<unsigned>(static_cast<unsigned>(format), 7u)], w, h, vw, vh,
                    frames, secs, secs > 0 ? frames / secs : 0.0, secs > 0 ? cpu / secs * 100.0 : 0.0, openMs);
        check(frames > 0 && secs > 0 && frames / secs >= info.fps, "decode em tempo real (fps >= fps do arquivo)");
    }
}

// --- (b) ritmo da apresentação no host ------------------------------------------
// `acquire` = DesktopFrames.nativeAcquireFrame (devolve o serial, 0 = nada novo).
template <typename Acquire>
void pace_part(const char* label, double compFps, Acquire acquire) {
    // AUREA_SMOKE_STRESS=n: n threads de CPU cheia durante a medida (PC ocupado).
    std::atomic<bool> stopStress{false};
    std::vector<std::thread> stress;
    if (const char* n = std::getenv("AUREA_SMOKE_STRESS"))
        for (int i = 0; i < std::atoi(n); ++i) stress.emplace_back([&] {
            std::vector<unsigned char> mem(32u << 20);
            size_t at = 0;
            while (!stopStress.load(std::memory_order_relaxed)) { mem[at] += 1; at = (at + 4093) % mem.size(); }
        });
    struct Join { std::atomic<bool>& stop; std::vector<std::thread>& t; ~Join() { stop = true; for (auto& x : t) x.join(); } } join{stopStress, stress};
    // A "UI" mede com prioridade alta: num PC dividido com outros processos o
    // atraso de quem mede não pode virar quadro "perdido" do motor.
    const int oldPriority = GetThreadPriority(GetCurrentThread());
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    struct Restore { int p; ~Restore() { SetThreadPriority(GetCurrentThread(), p); } } restore{oldPriority};
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    // 1) Produtor: quando cada quadro fica pronto (bomba a ~0,25 ms).
    std::vector<double> gaps;
    double last = 0;
    double start = now_ms();
    while (now_ms() - start < 5000.0) {
        if (acquire()) {
            const double t = now_ms();
            if (last > 0) gaps.push_back(t - last);
            last = t;
        }
        short_wait_us(timer, 250);
    }
    const Spread g = spread(gaps);
    std::printf("       %s produtor: %zu quadros, intervalo medio %.2f ms, desvio %.2f, p95 %.2f, max %.2f\n",
                label, gaps.size() + 1, g.mean, g.sd, g.p95, g.max);
    // 2) Consumidor no vblank do monitor (como a UI): quadro novo ou repetido?
    auto output = primary_output();
    if (!output) { std::printf("       %s: sem saida DXGI para o vblank\n", label); if (timer) CloseHandle(timer); return; }
    std::vector<double> vblanks;
    std::vector<int> holds;   // vblanks que cada quadro ficou na tela
    long long lastSerial = 0, skipped = 0;
    int hold = 0;
    start = now_ms();
    while (now_ms() - start < 5000.0) {
        if (FAILED(output->WaitForVBlank())) break;
        vblanks.push_back(now_ms());
        short_wait_us(timer, 1000);   // trabalho da UI antes de pegar o quadro
        const long long serial = acquire();
        ++hold;
        if (serial) {
            if (lastSerial && serial > lastSerial + 1) skipped += serial - lastSerial - 1;
            if (lastSerial) holds.push_back(hold);
            lastSerial = serial;
            hold = 0;
        }
    }
    std::vector<double> periods;
    for (size_t i = 1; i < vblanks.size(); ++i) periods.push_back(vblanks[i] - vblanks[i - 1]);
    const Spread vp = spread(periods);
    const double hz = vp.mean > 0 ? 1000.0 / vp.mean : 0;
    const double expected = compFps > 0 ? hz / compFps : 1;
    const int lo = static_cast<int>(std::floor(expected + 1e-6)), hi = static_cast<int>(std::ceil(expected - 1e-6));
    int histo[6]{};
    long long longer = 0, shorter = 0;
    for (const int x : holds) {
        ++histo[std::min(x, 5)];
        if (x > hi) longer += x - hi;
        if (x < lo) ++shorter;
    }
    std::printf("       %s consumidor: monitor %.2f Hz (%zu vblanks), quadros mostrados %zu, cadencia ideal %.2f vblanks/quadro\n",
                label, hz, vblanks.size(), holds.size() + (lastSerial ? 1 : 0), expected);
    std::printf("       %s   permanencia (vblanks): 1=%d 2=%d 3=%d 4=%d 5+=%d | repetidos alem da cadencia=%lld curtos=%lld perdidos (nunca mostrados)=%lld\n",
                label, histo[1], histo[2], histo[3], histo[4], histo[5], longer, shorter, skipped);
    if (timer) CloseHandle(timer);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    // Nada toca nas caixas: a saída de áudio do motor escreve zeros (WindowsMedia.cpp).
    SetEnvironmentVariableA("AUREA_AUDIO_SILENT", "1");
    const std::filesystem::path outDir = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();

    // --- JVM -------------------------------------------------------------------
    const std::filesystem::path jvmPath = AUREA_SMOKE_JVM;
    SetDllDirectoryW(jvmPath.parent_path().parent_path().c_str());   // jdk/bin: dependências da jvm.dll
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
    if (!createVm || createVm(&vm, reinterpret_cast<void**>(&env), &args) != JNI_OK) {
        std::printf("FALHA: JNI_CreateJavaVM\n");
        return 2;
    }

    // --- aurea.dll ---------------------------------------------------------------
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dllPath = std::filesystem::path(exe).parent_path() / L"aurea.dll";
    g_aurea = LoadLibraryW(dllPath.c_str());
    if (!g_aurea) { std::printf("FALHA: aurea.dll nao carregou (%lu)\n", GetLastError()); return 2; }
    auto onLoad = sym<jint(JNICALL*)(JavaVM*, void*)>("JNI_OnLoad");
    check(onLoad(vm, nullptr) >= JNI_VERSION_1_6, "JNI_OnLoad");
    // Aqui não há as classes Kotlin (AureaEngine/HyperFaceDetector): o
    // FindClass falha e deixa exceção pendente — a UI de verdade as tem.
    if (env->ExceptionCheck()) env->ExceptionClear();

    auto nativeCreate = sym<jlong(JNICALL*)(JNIEnv*, jclass)>(ENGINE(nativeCreate));
    auto nativeInitialize = sym<jboolean(JNICALL*)(JNIEnv*, jclass, jlong, jfloat, jstring, jstring, jboolean, jlongArray,
                                                   jintArray, jstring)>(ENGINE(nativeInitialize));
    auto nativeStartupError = sym<jstring(JNICALL*)(JNIEnv*, jclass, jlong)>(ENGINE(nativeStartupError));
    auto nativeNewProject = sym<jboolean(JNICALL*)(JNIEnv*, jclass, jlong, jint, jint, jdouble, jstring, jfloatArray)>(
        ENGINE(nativeNewProject));
    auto nativeAddShape = sym<jlong(JNICALL*)(JNIEnv*, jclass, jlong, jint)>(ENGINE(nativeAddShape));
    auto nativeAddText = sym<jlong(JNICALL*)(JNIEnv*, jclass, jlong, jstring)>(ENGINE(nativeAddText));
    auto nativeAttachSurface = sym<jboolean(JNICALL*)(JNIEnv*, jclass, jlong, jobject, jint, jint)>(ENGINE(nativeAttachSurface));
    auto nativeResizeSurface = sym<void(JNICALL*)(JNIEnv*, jclass, jlong, jint, jint)>(ENGINE(nativeResizeSurface));
    auto nativeDetachSurface = sym<void(JNICALL*)(JNIEnv*, jclass, jlong)>(ENGINE(nativeDetachSurface));
    auto nativeSubmitCommands = sym<jint(JNICALL*)(JNIEnv*, jclass, jlong, jobject, jint, jobject, jint)>(
        ENGINE(nativeSubmitCommands));
    auto nativeShutdown = sym<void(JNICALL*)(JNIEnv*, jclass, jlong)>(ENGINE(nativeShutdown));
    auto nativeDestroy = sym<void(JNICALL*)(JNIEnv*, jclass, jlong)>(ENGINE(nativeDestroy));
    auto acquireFrame = sym<jlong(JNICALL*)(JNIEnv*, jclass, jlong, jobject, jint, jintArray)>(FRAMES(nativeAcquireFrame));
    auto gpuName = sym<jstring(JNICALL*)(JNIEnv*, jclass, jlong)>(FRAMES(nativeGpuName));

    const auto jstr = [&](const std::string& s) { return env->NewStringUTF(s.c_str()); };
    const auto str = [&](jstring s) {
        if (!s) return std::string();
        const char* c = env->GetStringUTFChars(s, nullptr);
        std::string r = c ? c : "";
        if (c) env->ReleaseStringUTFChars(s, c);
        return r;
    };

    // --- Motor ---------------------------------------------------------------------
    const std::filesystem::path work = std::filesystem::temp_directory_path() / L"aurea-pc-smoke";
    std::filesystem::create_directories(work / L"cache");
    std::filesystem::create_directories(work / L"docs");
    std::filesystem::create_directories(work / L"plugins");
    // A taxa que a UI passa: a do modo atual do monitor (Main.kt, displayMode).
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    float refresh = 60.0f;
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) refresh = static_cast<float>(mode.dmDisplayFrequency);
    if (const char* hz = std::getenv("AUREA_SMOKE_HZ")) refresh = static_cast<float>(std::atof(hz));   // comparação
    std::printf("       monitor: %.0f Hz (taxa passada ao motor)\n", refresh);
    const jlong h = nativeCreate(env, nullptr);
    check(h != 0, "nativeCreate");
    const bool initialized = nativeInitialize(env, nullptr, h, refresh, jstr((work / L"cache").string()),
                                              jstr((work / L"docs").string()), JNI_FALSE, nullptr, nullptr,
                                              jstr((work / L"plugins").string())) == JNI_TRUE;
    check(initialized, "nativeInitialize");
    if (!initialized) {
        std::printf("       erro: %s\n", str(nativeStartupError(env, nullptr, h)).c_str());
        return 1;
    }
    std::printf("       GPU: %s\n", str(gpuName(env, nullptr, h)).c_str());
    check(nativeNewProject(env, nullptr, h, 1920, 1080, 60.0, jstr("Fumaca PC"), nullptr) == JNI_TRUE, "nativeNewProject 1920x1080@60");
    check(nativeAddShape(env, nullptr, h, 0) > 0, "nativeAddShape");
    check(nativeAddText(env, nullptr, h, jstr("AUREA PC")) > 0, "nativeAddText");

    jclass objectClass = env->FindClass("java/lang/Object");
    jobject dummySurface = objectClass ? env->AllocObject(objectClass) : nullptr;
    check(nativeAttachSurface(env, nullptr, h, dummySurface, 1280, 720) == JNI_TRUE, "nativeAttachSurface 1280x720");

    std::vector<unsigned char> pixels(static_cast<size_t>(3840) * 2160 * 4);
    jobject buffer = env->NewDirectByteBuffer(pixels.data(), static_cast<jlong>(pixels.size()));
    jintArray info = env->NewIntArray(3);
    jint dims[3]{};
    jlong lastSerial = 0;

    // Bomba de quadros como a UI: chama a cada `pollMs` e conta os novos.
    struct Pump { int frames = 0; double maxGapMs = 0; };
    const auto pump = [&](double seconds, int pollMs) {
        Pump p;
        const auto start = Clock::now();
        auto lastNew = start;
        while (std::chrono::duration<double>(Clock::now() - start).count() < seconds) {
            const jlong serial = acquireFrame(env, nullptr, h, buffer, static_cast<jint>(pixels.size()), info);
            if (serial) {
                if (serial <= lastSerial) ++g_failures;   // número de série precisa crescer
                lastSerial = serial;
                env->GetIntArrayRegion(info, 0, 3, dims);
                const auto now = Clock::now();
                if (p.frames) p.maxGapMs = std::max(p.maxGapMs, std::chrono::duration<double, std::milli>(now - lastNew).count());
                lastNew = now;
                ++p.frames;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        }
        return p;
    };

    // 1) Primeiro quadro: pixels sãos.
    const Pump first = pump(1.5, 1);
    check(first.frames >= 1, "quadro entregue apos o anexo");
    check(dims[0] == 1280 && dims[1] == 720 && dims[2] == 1280 * 4, "tamanho 1280x720, linhas de 5120 bytes");
    {
        size_t nonZero = 0;
        unsigned minA = 255;
        for (size_t i = 0; i < static_cast<size_t>(dims[1]) * dims[2]; i += 4) {
            nonZero += (pixels[i] | pixels[i + 1] | pixels[i + 2]) != 0;
            minA = std::min<unsigned>(minA, pixels[i + 3]);
        }
        const unsigned char* corner = pixels.data();
        const unsigned char* center = pixels.data() + (static_cast<size_t>(dims[1] / 2) * dims[2] + static_cast<size_t>(dims[0] / 2) * 4);
        std::printf("       canto BGRA=(%u,%u,%u,%u) centro BGRA=(%u,%u,%u,%u) pixels nao pretos=%zu alfa minimo=%u\n",
                    corner[0], corner[1], corner[2], corner[3], center[0], center[1], center[2], center[3], nonZero, minA);
        // Forma + texto brancos sobre a composição preta (que ocupa 16:9 inteiro).
        check(nonZero > 10000, "quadro nao e nulo (forma e texto desenhados)");
        check(std::memcmp(corner, center, 3) != 0, "palco diferente da mesa");
        check(minA == 255, "opaco (alfa 255: pre-multiplicado = reto)");
        write_bmp(outDir / L"smoke_frame.bmp", pixels.data(), dims[0], dims[1], dims[2]);
    }

    // 2) Tocando: ritmo do "vsync" do host.
    aurea::Command play;
    play.type = aurea::CommandType::PlaybackPlay;
    jobject cmdBuffer = env->NewDirectByteBuffer(&play, sizeof(play));
    check(nativeSubmitCommands(env, nullptr, h, cmdBuffer, 1, nullptr, 0) == 1, "PlaybackPlay");
    (void)pump(0.5, 1);   // aquecimento
    const double cpuPlay0 = process_cpu_seconds();
    const Pump playing = pump(3.0, 1);
    const double cpuPlay = process_cpu_seconds() - cpuPlay0;
    const double fps = playing.frames / 3.0;
    std::printf("       tocando: %.1f quadros/s, maior intervalo %.1f ms, CPU do processo %.1f%% de um nucleo (bomba a 1 ms incluida)\n",
                fps, playing.maxGapMs, cpuPlay / 3.0 * 100.0);
    check(fps > 50.0 && fps < 66.0, "ritmo perto de 60 quadros/s");

    // 3) Parado: nada muda → nada desenha.
    aurea::Command pause;
    pause.type = aurea::CommandType::PlaybackPause;
    jobject pauseBuffer = env->NewDirectByteBuffer(&pause, sizeof(pause));
    check(nativeSubmitCommands(env, nullptr, h, pauseBuffer, 1, nullptr, 0) == 1, "PlaybackPause");
    (void)pump(1.5, 16);   // assenta (refinamento do quadro parado)
    const double cpuIdle0 = process_cpu_seconds();
    const Pump idle = pump(4.0, 16);
    const double cpuIdle = process_cpu_seconds() - cpuIdle0;
    std::printf("       parado: %d quadros novos em 4 s, CPU do processo %.2f%% de um nucleo (bomba a 16 ms incluida)\n",
                idle.frames, cpuIdle / 4.0 * 100.0);
    check(idle.frames <= 1, "parado nao desenha");
    // O processo inclui a JVM (JIT/GC acordam sozinhos, 0-2% visto) e a bomba:
    // o que prova o custo zero do motor é não haver quadro novo acima.
    check(cpuIdle / 4.0 < 0.05, "custo parado do processo < 5% de um nucleo");

    // 4) Redimensionar: o próximo quadro sai no tamanho novo.
    nativeResizeSurface(env, nullptr, h, 1600, 900);
    (void)pump(1.0, 1);
    check(dims[0] == 1600 && dims[1] == 900, "redimensionado para 1600x900");

    // 5) (b) Ritmo tocando, medido como a UI vê: 60 e 30 quadros/s.
    if (part('b')) {
        std::printf("-- (b) ritmo da apresentacao no host\n");
        const auto acquire = [&]() -> long long {
            const jlong serial = acquireFrame(env, nullptr, h, buffer, static_cast<jint>(pixels.size()), info);
            if (serial && serial <= lastSerial) ++g_failures;
            if (serial) lastSerial = serial;
            return serial;
        };
        check(nativeSubmitCommands(env, nullptr, h, cmdBuffer, 1, nullptr, 0) == 1, "PlaybackPlay (60)");
        (void)pump(0.5, 1);
        pace_part("comp 60:", 60.0, acquire);
        check(nativeSubmitCommands(env, nullptr, h, pauseBuffer, 1, nullptr, 0) == 1, "PlaybackPause");
        check(nativeNewProject(env, nullptr, h, 1920, 1080, 30.0, jstr("Fumaca PC 30"), nullptr) == JNI_TRUE, "nativeNewProject 1920x1080@30");
        check(nativeAddShape(env, nullptr, h, 0) > 0, "nativeAddShape");
        check(nativeAddText(env, nullptr, h, jstr("AUREA PC 30")) > 0, "nativeAddText");
        (void)pump(0.5, 1);
        check(nativeSubmitCommands(env, nullptr, h, cmdBuffer, 1, nullptr, 0) == 1, "PlaybackPlay (30)");
        (void)pump(0.5, 1);
        pace_part("comp 30:", 30.0, acquire);
        check(nativeSubmitCommands(env, nullptr, h, pauseBuffer, 1, nullptr, 0) == 1, "PlaybackPause");
    }

    // 6) (v) Vídeo de verdade no preview: decode do MF → planos → GPU → UI.
    if (part('v')) {
        std::filesystem::path video = argc > 2 ? std::filesystem::path(argv[2])
            : std::filesystem::path(exe).parent_path().parent_path().parent_path().parent_path().parent_path()
                / L"build" / L"playback-matrix" / L"h264-1080p60.mp4";
        std::printf("-- (v) video no preview: %ls\n", video.filename().c_str());
        auto nativeImportVideo = sym<jlong(JNICALL*)(JNIEnv*, jclass, jlong, jstring, jstring)>(ENGINE(nativeImportVideo));
        check(nativeNewProject(env, nullptr, h, 1920, 1080, 60.0, jstr("Fumaca PC video"), nullptr) == JNI_TRUE, "nativeNewProject (video)");
        const jlong layer = nativeImportVideo(env, nullptr, h, jstr(aurea::windows::utf8(video.wstring())), jstr("video"));
        check(layer > 0, "nativeImportVideo");
        const auto acquire = [&]() -> long long {
            const jlong serial = acquireFrame(env, nullptr, h, buffer, static_cast<jint>(pixels.size()), info);
            if (serial && serial <= lastSerial) ++g_failures;
            if (serial) { lastSerial = serial; env->GetIntArrayRegion(info, 0, 3, dims); }
            return serial;
        };
        (void)pump(1.0, 1);
        check(nativeSubmitCommands(env, nullptr, h, cmdBuffer, 1, nullptr, 0) == 1, "PlaybackPlay (video)");
        (void)pump(0.5, 1);
        pace_part("video 60:", 60.0, acquire);
        auto nativePlaybackReport = sym<jstring(JNICALL*)(JNIEnv*, jclass, jlong)>(ENGINE(nativePlaybackReport));
        std::string report = str(nativePlaybackReport(env, nullptr, h));
        std::replace(report.begin(), report.end(), '\n', ' ');
        std::printf("       relatorio do motor: %s\n", report.c_str());
        check(nativeSubmitCommands(env, nullptr, h, pauseBuffer, 1, nullptr, 0) == 1, "PlaybackPause");
        size_t lit = 0;
        for (size_t i = 0; i < static_cast<size_t>(dims[1]) * dims[2]; i += 4) lit += (pixels[i] | pixels[i + 1] | pixels[i + 2]) > 16;
        check(lit > static_cast<size_t>(dims[0]) * dims[1] / 4, "quadro do video aparece (nao preto)");
        write_bmp(outDir / L"smoke_video.bmp", pixels.data(), dims[0], dims[1], dims[2]);
    }

    nativeDetachSurface(env, nullptr, h);
    check(acquireFrame(env, nullptr, h, buffer, static_cast<jint>(pixels.size()), info) == 0, "sem quadro depois do detach");
    nativeShutdown(env, nullptr, h);
    nativeDestroy(env, nullptr, h);

    if (part('a')) audio_part();
    if (part('c')) {
        std::vector<std::filesystem::path> files;
        for (int i = 2; i < argc; ++i) files.emplace_back(argv[i]);
        if (files.empty()) {
            // engine/build/pc-jni/bin → raiz do repositório.
            const auto root = std::filesystem::path(exe).parent_path().parent_path().parent_path().parent_path().parent_path();
            files.push_back(root / L"build" / L"playback-matrix" / L"h264-1080p60.mp4");
            files.push_back(root / L"build" / L"playback-matrix" / L"hevc-1080p30.mp4");
        }
        decode_part(files);
    }
    std::printf("%s (%d falhas)\n", g_failures ? "FUMACA FALHOU" : "FUMACA OK", g_failures);
    // A JVM fica viva: DestroyJavaVM espera threads que não são nossas.
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), g_failures ? 1 : 0);
    return g_failures ? 1 : 0;
}
