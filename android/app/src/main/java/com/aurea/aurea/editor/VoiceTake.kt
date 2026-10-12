package com.aurea.aurea.editor

import java.util.Locale
import kotlin.math.log10

/**
 * As contas puras do "Gravar voz" (sem Android: testadas na JVM). O iOS faz
 * as MESMAS contas em `VoiceTake` (VoiceRecorder.swift).
 */
internal object VoiceTake {
    /** Abaixo disso a tomada é um toque acidental: não vira camada. */
    const val MIN_DURATION_MS = 300L

    /** Faixa do medidor: −50 dBFS (silêncio de sala) até 0 dBFS (estouro). */
    private const val FLOOR_DB = -50f

    /**
     * Próximo nome livre "Voz N": 1 + o maior N já usado por uma camada
     * "<prefixo> N" (apagar a "Voz 2" não faz a próxima repetir um nome que
     * ainda existe; renomeadas não contam).
     */
    fun nextName(prefix: String, existing: List<String>): String {
        val head = "$prefix "
        var highest = 0
        for (name in existing) {
            if (!name.startsWith(head)) continue
            val n = name.substring(head.length).trim().toIntOrNull() ?: continue
            if (n > highest) highest = n
        }
        return "$prefix ${highest + 1}"
    }

    /** "mm:ss" do tempo gravado (minutos passam de 59 sem virar hora). */
    fun elapsedLabel(ms: Long): String {
        val total = (ms.coerceAtLeast(0L) / 1000L)
        return String.format(Locale.ROOT, "%02d:%02d", total / 60L, total % 60L)
    }

    /** Nível 0..1 do medidor a partir de dBFS (o `averagePower` do iOS). */
    fun levelFromDb(db: Float): Float {
        if (db.isNaN()) return 0f
        return ((db - FLOOR_DB) / -FLOOR_DB).coerceIn(0f, 1f)
    }

    /** Nível 0..1 a partir do pico do MediaRecorder (`maxAmplitude`, 0..32767). */
    fun levelFromAmplitude(amplitude: Int): Float {
        if (amplitude <= 0) return 0f
        return levelFromDb(20f * log10(amplitude.coerceAtMost(32767) / 32767f))
    }

    /**
     * O medidor sobe na hora e desce devagar (decaimento do pico): sem isso a
     * barra pisca entre sílabas e não dá para ler.
     */
    fun smooth(previous: Float, next: Float): Float = maxOf(next, previous * 0.82f)
}
