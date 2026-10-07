package com.aurea.aurea.engine

/**
 * One-second sampling is independent of deprecated Android running-memory callbacks
 * (Android 14+ no longer delivers TRIM_MEMORY_RUNNING_*).
 *
 * `lowMemoryClass` ([DeviceMemoryClass]): aparelhos de até ~4 GB (Galaxy A15/A16,
 * realme RMX2020, moto g52) — o LMKD da Samsung mata o app EM PRIMEIRO PLANO
 * (LOW_MEMORY, importância 100) com folga maior que a dos outros. Ali a
 * reserva acima do limiar do sistema sobe de 64/128 MB para 96/192 MB.
 */
internal class MemoryPressurePolicy(private val lowMemoryClass: Boolean = false) {
    private var lastLevel = 0
    private var lastTrimMs = Long.MIN_VALUE
    private val criticalReserve = if (lowMemoryClass) 96L shl 20 else 64L shl 20
    private val lowReserve = if (lowMemoryClass) 192L shl 20 else 128L shl 20

    fun nextTrim(nowMs: Long, systemLow: Boolean, heapHeadroom: Long, heapMaximum: Long,
                 systemAvailable: Long = Long.MAX_VALUE, systemThreshold: Long = 0): Int {
        val critical = maxOf(16L shl 20, heapMaximum / 20)
        val low = maxOf(32L shl 20, heapMaximum / 10)
        // Java's heap can be healthy while native/GPU allocations exhaust RAM.
        // Reserve space ABOVE Android's device-specific low-memory threshold;
        // unlike iOS jetsam headroom, availMem is shared by the whole system.
        val systemHeadroom = systemAvailable.coerceAtLeast(0) - systemThreshold.coerceAtLeast(0)
        val level = when {
            systemLow || heapHeadroom <= critical || systemHeadroom <= criticalReserve -> 15
            heapHeadroom <= low || systemHeadroom <= lowReserve -> 10
            else -> 0
        }
        if (level == 0) { lastLevel = 0; return 0 }
        if (level <= lastLevel && lastTrimMs != Long.MIN_VALUE && nowMs - lastTrimMs < 10_000) return 0
        lastLevel = level
        lastTrimMs = nowMs
        return level
    }
}

/**
 * Classe de memória LOW do lado Kotlin — o mesmo corte do motor
 * (DeviceCapabilities memory_tier: RAM total abaixo de 4608 MiB), mais
 * [android.app.ActivityManager.isLowRamDevice] e heap por app (memoryClass)
 * pequeno. Os caches de bitmap da UI e o limiar de pressão seguem a classe.
 */
internal object DeviceMemoryClass {
    const val LOW_TOTAL_BYTES = 4608L shl 20
    const val LOW_MEMORY_CLASS_MB = 192

    /** Medido uma vez no início do processo ([init]); false até lá. */
    @Volatile var low: Boolean = false
        private set

    fun isLow(totalMemBytes: Long, lowRamDevice: Boolean, memoryClassMb: Int): Boolean =
        lowRamDevice || totalMemBytes in 1 until LOW_TOTAL_BYTES || memoryClassMb in 1..LOW_MEMORY_CLASS_MB

    fun init(context: android.content.Context) {
        runCatching {
            val am = context.getSystemService(android.content.Context.ACTIVITY_SERVICE) as? android.app.ActivityManager
                ?: return
            val info = android.app.ActivityManager.MemoryInfo()
            am.getMemoryInfo(info)
            low = isLow(info.totalMem, am.isLowRamDevice, am.memoryClass)
        }
    }

    /** Bitmaps de miniatura da timeline: heap/16 entre 8 e 24 MB; 8 MB na classe LOW. */
    fun thumbnailCacheBytes(maxHeapBytes: Long, low: Boolean): Int =
        if (low) 8 shl 20 else (maxHeapBytes / 16).coerceIn(8L shl 20, 24L shl 20).toInt()

    /** Prévias do navegador de efeitos em memória: 16 MB; 8 MB na classe LOW. */
    fun effectPreviewCacheBytes(low: Boolean): Int = if (low) 8 shl 20 else 16 shl 20
}

internal interface TrimmableImageCache { fun releaseImages() }

/** Weak registrations never extend a screen/ViewModel's lifetime. */
internal object UiImageCaches {
    private val caches = java.util.WeakHashMap<TrimmableImageCache, Unit>()
    fun register(cache: TrimmableImageCache) { synchronized(caches) { caches[cache] = Unit } }
    fun trim() {
        val current = synchronized(caches) { caches.keys.toList() }
        current.forEach { it.releaseImages() }
    }
}
