#include "aurea/core/Thread.hpp"

#if defined(AUREA_PLATFORM_ANDROID)
    #include <dlfcn.h>
    #include <pthread.h>
    #include <sys/resource.h>
    #include <unistd.h>
#elif defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#elif defined(__APPLE__)
    #include <pthread.h>
    #include <pthread/qos.h>
#elif defined(__linux__)
    #include <pthread.h>
#endif

namespace aurea {

void set_current_thread_priority(ThreadPriority p) noexcept {
#if defined(AUREA_PLATFORM_ANDROID)
    // Valores de android/os/Process.java: DISPLAY = -4, URGENT_DISPLAY = -8, AUDIO = -16,
    // BACKGROUND = 10. `setpriority` por tid é o que o próprio framework usa.
    int nice = 0;
    switch (p) {
        case ThreadPriority::Background: nice = 10; break;
        case ThreadPriority::Normal:     nice = 0;  break;
        case ThreadPriority::Decode:     nice = -4; break;
        case ThreadPriority::Display:    nice = -8; break;
        case ThreadPriority::Audio:      nice = -16; break;   // AUDIO
        case ThreadPriority::Export:     nice = -8; break;    // URGENT_DISPLAY
    }
    (void)setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), nice);
#elif defined(_WIN32)
    int prio = THREAD_PRIORITY_NORMAL;
    switch (p) {
        case ThreadPriority::Background: prio = THREAD_PRIORITY_BELOW_NORMAL; break;
        case ThreadPriority::Normal:     prio = THREAD_PRIORITY_NORMAL; break;
        case ThreadPriority::Decode:     prio = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case ThreadPriority::Display:    prio = THREAD_PRIORITY_HIGHEST; break;
        case ThreadPriority::Audio:      prio = THREAD_PRIORITY_TIME_CRITICAL; break;
        case ThreadPriority::Export:     prio = THREAD_PRIORITY_ABOVE_NORMAL; break;
    }
    (void)SetThreadPriority(GetCurrentThread(), prio);
#elif defined(__APPLE__)
    // iOS/macOS: só o export ganha QoS explícito (userInitiated: o usuário
    // espera o arquivo). As outras faixas ficam como sempre (QoS herdado).
    if (p == ThreadPriority::Export) (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#else
    (void)p;
#endif
}

#if defined(AUREA_PLATFORM_ANDROID)
namespace {
// android/performance_hint.h (API 33). Carregado por dlsym: o app roda desde a
// API 26 e o símbolo não existe abaixo de 33.
struct HintApi {
    void* (*getManager)() = nullptr;
    void* (*createSession)(void*, const i32*, size_t, i64) = nullptr;
    int (*reportActual)(void*, i64) = nullptr;
    void (*closeSession)(void*) = nullptr;
    bool ok = false;
};
const HintApi& hint_api() noexcept {
    static const HintApi api = [] {
        HintApi a;
        void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return a;
        a.getManager = reinterpret_cast<void* (*)()>(dlsym(lib, "APerformanceHint_getManager"));
        a.createSession = reinterpret_cast<void* (*)(void*, const i32*, size_t, i64)>(
            dlsym(lib, "APerformanceHint_createSession"));
        a.reportActual = reinterpret_cast<int (*)(void*, i64)>(dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
        a.closeSession = reinterpret_cast<void (*)(void*)>(dlsym(lib, "APerformanceHint_closeSession"));
        a.ok = a.getManager && a.createSession && a.reportActual && a.closeSession;
        return a;
    }();
    return api;
}
} // namespace
#endif

PerformanceHintSession::~PerformanceHintSession() { close(); }

bool PerformanceHintSession::open_for_current_thread(i64 targetNs) noexcept {
    close();
#if defined(AUREA_PLATFORM_ANDROID)
    const HintApi& api = hint_api();
    if (!api.ok || targetNs <= 0) return false;
    void* manager = api.getManager();
    if (!manager) return false;
    const i32 tid = static_cast<i32>(gettid());
    session_ = api.createSession(manager, &tid, 1, targetNs);
    return session_ != nullptr;
#else
    (void)targetNs;
    return false;
#endif
}

void PerformanceHintSession::report(i64 actualNs) noexcept {
#if defined(AUREA_PLATFORM_ANDROID)
    if (session_ && actualNs > 0) (void)hint_api().reportActual(session_, actualNs);
#else
    (void)actualNs;
#endif
}

void PerformanceHintSession::close() noexcept {
#if defined(AUREA_PLATFORM_ANDROID)
    if (session_) hint_api().closeSession(session_);
#endif
    session_ = nullptr;
}

void set_current_thread_name(const char* name) noexcept {
    if (!name) return;
#if defined(AUREA_PLATFORM_ANDROID) || defined(__linux__)
    (void)pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    (void)pthread_setname_np(name);
#elif defined(_WIN32)
    wchar_t wide[64]{};
    for (int i = 0; i < 63 && name[i]; ++i) wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
    (void)SetThreadDescription(GetCurrentThread(), wide);
#endif
}

} // namespace aurea
