#pragma once
#include "aurea/core/Types.hpp"
#include <algorithm>
#include <cmath>

namespace aurea {
// Teto de quadros compostos guardados (10 s a 30 fps). O orçamento em bytes
// continua mandando: a capacidade é o que cabe nele, nunca acima disto.
inline constexpr u32 kPreviewCacheMaxFrames = 300;

// Orçamento da prévia em memória pela RAM do aparelho (Android: ActivityManager
// totalMem; iOS: ProcessInfo.physicalMemory — os dois chegam em
// DeviceCapabilities::cpu().totalMemoryBytes). Faixas: 32 MiB (pouca RAM),
// 64 MiB (< 4 GB), 320 MiB (< 8 GB), 512 MiB. Nunca passa de 1/4 do orçamento
// do processo e encolhe com a pressão de memória (a partir de .85 o cache já é
// desligado pelo motor; a política aqui só antecipa).
// Aparelho de classe de memória LOW (até 4 GB: Galaxy A15/A16 de 4 GB, realme
// RMX2020, moto g52): o LMKD do Android mata o app EM PRIMEIRO PLANO por pouca
// memória (ApplicationExitInfo LOW_MEMORY, importância 100). Ali a prévia
// guardada nunca passa de 32 MiB, qualquer que seja a faixa de RAM.
inline constexpr u64 kPreviewCacheLowClassBudget = 32ull << 20;
// Depois de um aviso de pressão (RUNNING_LOW ou acima) a prévia guardada fica
// desligada por este tempo: soltar e reencher na mesma hora devolveria a
// memória que o sistema acabou de pedir. Cada aviso novo renova o prazo.
inline constexpr u64 kPreviewPressureHoldNs = 20'000'000'000ull;

inline u64 preview_cache_budget(u64 totalRamBytes, u64 processBudgetBytes, f32 pressure = 0.f,
                                bool lowMemoryClass = false) noexcept {
    constexpr u64 MiB = 1ull << 20, GiB = 1ull << 30;
    u64 tier = !totalRamBytes ? 64 * MiB
             : totalRamBytes < 2560 * MiB ? 32 * MiB
             : totalRamBytes < 4 * GiB ? 64 * MiB
             : totalRamBytes < 8 * GiB ? 320 * MiB : 512 * MiB;
    if (lowMemoryClass) tier = std::min(tier, kPreviewCacheLowClassBudget);
    if (processBudgetBytes) tier = std::min(tier, processBudgetBytes / 4);
    if (!std::isfinite(pressure) || pressure >= .85f) return 0;
    if (pressure >= .7f) return tier / 4;
    if (pressure >= .5f) return tier / 2;
    return tier;
}

// Bytes por pixel do quadro guardado: RGBA8 (sRGB) para prévia SDR, RGBA16F
// quando a saída é HDR. A alocação da GPU manda: um quadro que não cabe não
// reserva nada.
inline u32 preview_cache_capacity(u32 width, u32 height, u64 budget, u32 bytesPerPixel = 8) noexcept {
    if (!width || !height || !bytesPerPixel) return 0;
    const u64 pixels = static_cast<u64>(width) * height;
    if (pixels > budget / bytesPerPixel) return 0;
    return static_cast<u32>(std::min<u64>(kPreviewCacheMaxFrames, budget / (pixels * bytesPerPixel)));
}
inline u32 preview_cache_target(f64 fps, u32 capacity, i64 remaining, f32 speed = 1.f) noexcept {
    if (!capacity || remaining <= 0) return 0;
    // Match the decoder's short look-ahead instead of blocking every Play for
    // half a second of expensive effects. Faster playback needs more frames.
    const f64 rate = std::clamp(std::isfinite(speed) ? std::abs(f64(speed)) : 1., .05, 16.);
    const auto startup = static_cast<u32>(std::clamp(std::ceil(
        std::clamp(std::isfinite(fps) ? fps : 30., 1., 240.) * .18 * rate), 1., 30.));
    return static_cast<u32>(std::min<i64>(std::min(capacity, startup), remaining));
}
inline bool preview_buffer_expired(u64 elapsedNs, u32 ready) noexcept {
    // A partial usable buffer starts promptly; missing media/AI gets a finite
    // first-frame allowance. This never labels incomplete frames as cached.
    return elapsedNs >= (ready ? 350'000'000ull : 1'200'000'000ull);
}
inline u32 preview_buffer_status(u32 ready, u32 target, bool buffering, bool limited = false) noexcept {
    return std::min(ready, 255u) | (std::min(target, 255u) << 8)
        | (buffering ? 0x80000000u : 0u) | (limited ? 0x40000000u : 0u);
}

// Idle preparation has its own clock. It never advances playback or extends
// the cache budget; the look-ahead fills the whole composition cache (up to
// kPreviewCacheMaxFrames), not just one second.
inline u32 preview_idle_target(f64 fps, u32 capacity, i64 remaining) noexcept {
    (void)fps;
    if (!capacity || remaining <= 0) return 0;
    return static_cast<u32>(std::min<i64>(std::min(capacity, kPreviewCacheMaxFrames), remaining));
}

struct PreviewIdleKey {
    u64 session = 0, revision = 0, composition = 0, mediaEpoch = 0;
    i64 playhead = 0;
    bool operator==(const PreviewIdleKey&) const = default;
};

class PreviewIdleBuffer {
    PreviewIdleKey key_{};
    u64 due_ = 0, blockedMedia_ = 0;
    bool active_ = false, done_ = false, blocked_ = false;
public:
    void observe(bool eligible, PreviewIdleKey key, u64 now, bool changed) noexcept {
        if (!eligible) { *this = {}; return; }
        if (!active_ || key != key_ || changed) {
            key_ = key; active_ = true; done_ = blocked_ = false;
            due_ = now + 650'000'000ull;
        }
    }
    u64 deadline(u64 mediaReady) const noexcept {
        return active_ && !done_ && (!blocked_ || mediaReady != blockedMedia_) ? due_ : 0;
    }
    bool ready(u64 now, u64 mediaReady) const noexcept {
        const u64 due = deadline(mediaReady);
        return due && now >= due;
    }
    void attempted(u64 now, u64 workNs, bool complete, u64 mediaReady) noexcept {
        blocked_ = !complete; blockedMedia_ = mediaReady;
        // Give interactive work priority even when a composition is cheap.
        // Pending media parks until a completion callback instead of polling GPU.
        // A pause is half of the work just done: idle fill keeps going at
        // ~2/3 duty instead of one frame per 32 ms (300 frames took > 10 s).
        const u64 rest = complete ? std::clamp<u64>(workNs / 2, 4'000'000ull, 250'000'000ull)
                                  : 100'000'000ull;
        due_ = now + rest;
    }
    void finish() noexcept { done_ = true; due_ = 0; }
};
}
