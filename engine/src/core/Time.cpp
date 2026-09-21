#include "aurea/core/Time.hpp"

#if defined(AUREA_PLATFORM_ANDROID)
    #include <time.h>
    #include <sys/time.h>
#elif defined(AUREA_PLATFORM_IOS)
    #include <mach/mach_time.h>
    #include <sys/time.h>
#else
    #include <chrono>
    #include <ctime>
#endif

namespace aurea {

#if defined(AUREA_PLATFORM_IOS)

u64 monotonic_ns() noexcept {
    // mach_absolute_time é o relógio monotônico do Darwin; o timebase é
    // consultado uma vez e cacheado com inicialização estática thread-safe.
    static const double nsPerTick = [] {
        mach_timebase_info_data_t tb{};
        mach_timebase_info(&tb);
        return static_cast<double>(tb.numer) / static_cast<double>(tb.denom);
    }();
    return static_cast<u64>(static_cast<double>(mach_absolute_time()) * nsPerTick);
}

u64 monotonic_frequency() noexcept { return 1'000'000'000ull; }

u64 wall_clock_ms() noexcept {
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    return static_cast<u64>(tv.tv_sec) * 1000ull
         + static_cast<u64>(tv.tv_usec) / 1000ull;
}

#elif defined(AUREA_PLATFORM_ANDROID)

u64 monotonic_ns() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<u64>(ts.tv_sec) * 1'000'000'000ull
         + static_cast<u64>(ts.tv_nsec);
}

u64 monotonic_frequency() noexcept { return 1'000'000'000ull; }

u64 wall_clock_ms() noexcept {
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    return static_cast<u64>(tv.tv_sec) * 1000ull
         + static_cast<u64>(tv.tv_usec) / 1000ull;
}

#else   // host — Windows, Linux, macOS de desenvolvimento

u64 monotonic_ns() noexcept {
    using clock = std::chrono::steady_clock;
    static const clock::time_point origin = clock::now();
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - origin).count());
}

u64 monotonic_frequency() noexcept { return 1'000'000'000ull; }

u64 wall_clock_ms() noexcept {
    using clock = std::chrono::system_clock;
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now().time_since_epoch()).count());
}

#endif

} // namespace aurea
