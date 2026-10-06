package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackProperty
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Vetor de `queryShapeParams`: os valores dos parâmetros (o 0 é o tipo) e, no
 * fim, os bits de animado e de keyframe aqui. O motor passou de 7 para 15
 * parâmetros; o painel lê os bits pelo fim, não por índice fixo.
 */
class ShapeParamsTest {
    private fun params(count: Int, anim: Int, here: Int) =
        FloatArray(count + 2) { it.toFloat() }.also { it[count] = anim.toFloat(); it[count + 1] = here.toFloat() }

    @Test fun bitsComeFromTheTailForOldAndNewLayouts() {
        val old = params(7, (1 shl 5) or (1 shl 6), 1 shl 5)
        assertEquals((1 shl 5) or (1 shl 6), shapeAnimBits(old))
        assertEquals(1 shl 5, shapeKeyBits(old))
        val new = params(15, (1 shl ShapeParam.DEPTH) or (1 shl ShapeParam.SEED), 1 shl ShapeParam.SEED)
        assertEquals((1 shl ShapeParam.DEPTH) or (1 shl ShapeParam.SEED), shapeAnimBits(new))
        assertEquals(1 shl ShapeParam.SEED, shapeKeyBits(new))
        // O valor do parâmetro 7 é o 7º número — não é mais o bit de animado.
        assertEquals(ShapeParam.DEPTH.toFloat(), new[ShapeParam.DEPTH], 0f)
    }

    @Test fun missingOrEmptyVectorMeansNoAnimation() {
        assertEquals(0, shapeAnimBits(null))
        assertEquals(0, shapeKeyBits(null))
        assertEquals(0, shapeAnimBits(FloatArray(0)))
        assertEquals(0, shapeKeyBits(FloatArray(1)))
    }

    @Test fun everyEngineShapeTypeIsInTheSwitcherOnce() {
        // 2 (caminho livre) e 11 (vetorial) não são formas SDF.
        val expected = (0..24).filter { it != 2 && it != 11 }
        assertEquals(expected, SimpleShapes.toList())
    }

    @Test fun newParameterTracksAreFoundByIndex() {
        val sweep = listOf(
            KeyframeRow(TrackProperty.SHAPE_PARAM, -1, 0, 90f, 1, ShapeParam.SWEEP),
            KeyframeRow(TrackProperty.SHAPE_PARAM, -1, 30, 270f, 1, ShapeParam.SWEEP),
        )
        assertEquals(sweep, sweep.shapeTrack(ShapeParam.SWEEP))
        assertTrue(sweep.shapeTrack(ShapeParam.DEPTH).isEmpty())
    }
}
