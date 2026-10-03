package com.aurea.aurea.engine

import android.app.ActivityManager
import android.content.Context

/** Qualidades do "Otimizar modelo" — os MESMOS números de scene3d::ModelQuality (ModelBudget.hpp). */
const val MODEL_QUALITY_ORIGINAL = 0
const val MODEL_QUALITY_BALANCED = 1
const val MODEL_QUALITY_LIGHT = 2

/**
 * O plano de import de um modelo 3D que o motor devolve ANTES de importar
 * (Engine::inspect_model). Layout dos slots em aurea_jni.cpp (nativeInspectModel).
 * A conta é toda do motor; aqui só se lê.
 */
class ModelPlan(private val v: LongArray) {
    private fun at(i: Int): Long = if (i < v.size) v[i] else 0L

    val valid: Boolean get() = at(0) != 0L
    /** false = estimativa (FBX: as contagens só existem depois de ler a geometria). */
    val exact: Boolean get() = at(1) != 0L
    /** O Original não cabe (ou é denso demais para o preview): oferecer "Otimizar modelo". */
    val heavy: Boolean get() = valid && at(2) != 0L
    /** Nem o Leve cabe neste aparelho: recusar com o motivo, sem tentar. */
    val tooHeavy: Boolean get() = valid && at(3) != 0L
    val recommended: Int get() = at(4).toInt().coerceIn(MODEL_QUALITY_ORIGINAL, MODEL_QUALITY_LIGHT)
    val triangles: Long get() = at(5)
    val textures: Int get() = at(7).toInt()
    val largestTextureSide: Int get() = at(8).toInt()
    val budgetBytes: Long get() = at(9)
    fun fits(quality: Int): Boolean = at(10 + quality.coerceIn(0, 2)) != 0L
    fun peakBytes(quality: Int): Long = at(13 + quality.coerceIn(0, 2))
    fun keptTriangles(quality: Int): Long = at(16 + quality.coerceIn(0, 2))
    fun textureCap(quality: Int): Int = at(19 + quality.coerceIn(0, 2)).toInt()

    /** Qualidades oferecidas, da mais fiel à mais leve; o Original só quando cabe. */
    fun offered(): List<Int> = buildList {
        if (fits(MODEL_QUALITY_ORIGINAL)) add(MODEL_QUALITY_ORIGINAL)
        add(MODEL_QUALITY_BALANCED)
        add(MODEL_QUALITY_LIGHT)
    }

    companion object {
        /** [totalMem, availMem, isLowRamDevice] medidos agora — o motor completa o que faltar. */
        fun memoryNow(context: Context): LongArray? = try {
            val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            val mi = ActivityManager.MemoryInfo().also { am.getMemoryInfo(it) }
            longArrayOf(mi.totalMem, mi.availMem, if (am.isLowRamDevice) 1L else 0L)
        } catch (_: Exception) {
            null
        }

        /** "1,2 mi" / "850 mil" — número curto de triângulos para a mensagem. */
        fun shortCount(n: Long, thousand: String, million: String): String = when {
            n >= 1_000_000 -> String.format(java.util.Locale.getDefault(), "%.1f %s", n / 1_000_000.0, million)
            n >= 1_000 -> "${n / 1_000} $thousand"
            else -> n.toString()
        }
    }
}
