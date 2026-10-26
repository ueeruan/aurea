package com.aurea.aurea.editor.panels

import com.aurea.aurea.ui.ds.numeroPtBr
import kotlin.math.atan
import kotlin.math.roundToInt

/**
 * Contas puras da face LENTE (sem Compose, testáveis na JVM). A projeção segue
 * a distância focal como o motor: sensor full frame 36×24 mm, FOV vertical =
 * 2·atan(24 / 2f).
 */
internal object CameraLens {
    /** Faixa do arrasto (o teclado aceita [KEYPAD_MIN_MM]..[KEYPAD_MAX_MM]). */
    const val DRAG_MIN_MM = 8f
    const val DRAG_MAX_MM = 400f
    const val KEYPAD_MIN_MM = 4f
    const val KEYPAD_MAX_MM = 1200f
    const val APERTURE_MIN = 1.2f
    const val APERTURE_MAX = 22f
    const val BLUR_MAX_PERCENT = 300f
    private const val SENSOR_HEIGHT_MM = 24f

    /** Lentes prontas (mm), das grandes-angulares às teleobjetivas. */
    val PRESETS_MM: IntArray = intArrayOf(14, 18, 24, 35, 50, 85, 135, 200)

    /** FOV vertical (°) da distância focal; 0 se a focal não presta. */
    fun fovFromFocal(mm: Float): Float {
        if (!(mm > 0f) || !mm.isFinite()) return 0f
        return Math.toDegrees(2.0 * atan((SENSOR_HEIGHT_MM / 2f / mm).toDouble())).toFloat()
    }

    /** "FOV 46,8°" vira `lens_fov_readout` com este argumento. */
    fun formatFov(fovDegrees: Float): String = numeroPtBr(fovDegrees, 1)

    /** Distância focal arredondada: "50 mm". */
    fun formatFocal(mm: Float): String = "${mm.roundToInt()} mm"

    /** Abertura: "f/2.8" (ponto, como nas lentes). */
    fun formatAperture(f: Float): String = "f/" + numeroPtBr(f, 1).replace(',', '.')

    /** O preset marcado é o que bate com a focal atual (meio mm de folga). */
    fun presetIndex(mm: Float): Int = PRESETS_MM.indexOfFirst { kotlin.math.abs(it - mm) < 0.5f }

    // ---------------------------------------------------------------------------------
    // Omega aparência (área 05, P1-6): ângulo de visão e zoom da aba Visão (Opções da
    // câmera) e da face Escala da câmera. O motor grava a FOCAL (4–1200 mm); o ângulo
    // (FOV vertical) e o zoom são leituras dela.
    // ---------------------------------------------------------------------------------

    /** FOV padrão da câmera nova do motor (`CameraData::fov`); zoom 1× = esta lente. */
    const val DEFAULT_FOV = 50f

    /** Faixa do ângulo que a focal do motor alcança (a referência vai de 0,1° a 179,9°). */
    val MIN_FOV: Float = fovFromFocal(KEYPAD_MAX_MM)
    val MAX_FOV: Float = fovFromFocal(KEYPAD_MIN_MM)

    /** Focal (mm) do FOV vertical (°): `12 / tan(fov/2)`; 0 se o ângulo não presta. */
    fun focalFromFov(fovDegrees: Float): Float {
        if (!fovDegrees.isFinite() || fovDegrees <= 0f || fovDegrees >= 180f) return 0f
        return (SENSOR_HEIGHT_MM / 2.0 / kotlin.math.tan(Math.toRadians(fovDegrees / 2.0))).toFloat()
    }

    /** Zoom (×) da focal: 1× é a lente do FOV padrão. */
    fun zoomFromFocal(mm: Float): Float {
        val base = focalFromFov(DEFAULT_FOV)
        return if (base > 0f && mm.isFinite()) mm / base else 1f
    }

    /** Focal (mm) do zoom, presa na faixa do motor. */
    fun focalFromZoom(zoom: Float): Float =
        (zoom * focalFromFov(DEFAULT_FOV)).coerceIn(KEYPAD_MIN_MM, KEYPAD_MAX_MM)

    /** Faixa do zoom que a focal do motor alcança. */
    val MIN_ZOOM: Float get() = zoomFromFocal(KEYPAD_MIN_MM)
    val MAX_ZOOM: Float get() = zoomFromFocal(KEYPAD_MAX_MM)

    /** "1,25×" (duas casas). */
    fun formatZoom(zoom: Float): String = numeroPtBr(zoom, 2) + "×"
}
