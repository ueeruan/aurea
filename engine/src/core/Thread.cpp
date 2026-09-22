#include "aurea/core/Thread.hpp"

#if defined(AUREA_PLATFORM_ANDROID)
    #include <pthread.h>
    #include <sys/resource.h>
    #include <unistd.h>
#elif defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
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
    }
    (void)SetThreadPriority(GetCurrentThread(), prio);
#else
    (void)p;
#endif
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
