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
}
