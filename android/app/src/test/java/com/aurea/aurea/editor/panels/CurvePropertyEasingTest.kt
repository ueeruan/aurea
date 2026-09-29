package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackProperty
import org.junit.Assert.*
import org.junit.Test

/** Curva na propriedade inteira, presets prontos e o preset salvo (ida e volta). */
class CurvePropertyEasingTest {
    private fun key(property: Int, time: Int, value: Float, param: Int = 0, effect: Int = -1) =
        KeyframeRow(property, effect, time, value, 1, param)

    private fun starts(keys: List<KeyframeRow>, of: KeyframeRow) =
        propertySegmentStarts(keys, keys.track(of)).map { it.property to it.time }.toSet()

    @Test fun singlePropertyGetsEverySegmentButTheLastKey() {
        val keys = listOf(
            key(TrackProperty.OPACITY, 0, 0f), key(TrackProperty.OPACITY, 10, 1f),
            key(TrackProperty.OPACITY, 20, .5f), key(TrackProperty.OPACITY, 30, 1f),
            // Outra propriedade não entra.
            key(TrackProperty.ROTATION_Z, 0, 0f), key(TrackProperty.ROTATION_Z, 30, 90f),
        )
        assertEquals(
            setOf(TrackProperty.OPACITY to 0, TrackProperty.OPACITY to 10, TrackProperty.OPACITY to 20),
            starts(keys, keys[2]),
        )
    }

    @Test fun groupedXyzPropertyCoversAllAxes() {
        val keys = listOf(
            key(TrackProperty.POSITION_X, 0, 0f), key(TrackProperty.POSITION_X, 15, 5f), key(TrackProperty.POSITION_X, 30, 9f),
            key(TrackProperty.POSITION_Y, 0, 0f), key(TrackProperty.POSITION_Y, 30, 4f),
            key(TrackProperty.POSITION_Z, 5, 1f), key(TrackProperty.POSITION_Z, 25, 2f),
            // Escala é outro grupo; um eixo parado (1 marca) não abre trecho.
            key(TrackProperty.SCALE_X, 0, 1f), key(TrackProperty.SCALE_X, 30, 2f),
        )
        assertEquals(
            setOf(
                TrackProperty.POSITION_X to 0, TrackProperty.POSITION_X to 15,
                TrackProperty.POSITION_Y to 0, TrackProperty.POSITION_Z to 5,
            ),
            starts(keys, keys[3]),
        )
    }

    @Test fun effectComponentsOfTheSameParameterMoveTogether() {
        val p = TrackProperty.EFFECT_PARAM
        val keys = listOf(
            key(p, 0, 0f, param = 8, effect = 2), key(p, 20, 1f, param = 8, effect = 2),
            key(p, 0, 0f, param = 9, effect = 2), key(p, 20, 1f, param = 9, effect = 2),
            key(p, 0, 0f, param = 12, effect = 2), key(p, 20, 1f, param = 12, effect = 2),
        )
        val got = propertySegmentStarts(keys, keys.track(keys[0])).map { it.paramIndex }.toSet()
        assertEquals(setOf(8, 9), got)
    }

    @Test fun fewerThanTwoKeysIsRefused() {
        val one = listOf(key(TrackProperty.OPACITY, 0, 1f), key(TrackProperty.POSITION_Y, 0, 0f), key(TrackProperty.POSITION_Y, 9, 1f))
        assertFalse(curveEditable(one.track(one[0])))
        assertTrue(propertySegmentStarts(one, one.track(one[0])).isEmpty())
        assertTrue(curveEditable(one.track(one[1])))
        assertTrue(propertySegmentStarts(emptyList(), emptyList()).isEmpty())
    }

    @Test fun standardPresetsAreTheAgreedBeziers() {
        val eases = StandardCurves.map { it.second }
        val want = listOf(
            floatArrayOf(.33f, 0f, .66f, 1f), floatArrayOf(.42f, 0f, 1f, 1f), floatArrayOf(0f, 0f, .58f, 1f),
            floatArrayOf(.34f, 1.56f, .64f, 1f), floatArrayOf(.36f, 0f, .66f, -.56f),
        )
        assertEquals(want.size, eases.size)
        eases.zip(want).forEach { (e, w) ->
            assertEquals(Interp.BEZIER, e.interp)
            assertArrayEquals(w, e.handles(), 1e-6f)
        }
        // Passar do ponto sobe acima de 1; Antecipar desce abaixo de 0.
        assertTrue((1..99).any { eases[3].transform(it / 100f) > 1f })
        assertTrue((1..99).any { eases[4].transform(it / 100f) < 0f })
        // Nenhum repete o Bounce, que fica no botão próprio.
        assertTrue(eases.none { it.interp == Interp.BOUNCE })
    }

    @Test fun savedPresetRoundTripKeepsCurveAndPower() {
        val cases = listOf(
            Ease(Interp.BEZIER, .34f, 1.56f, .64f, 1f, 2),
            Ease(Interp.CUSTOM, .1f, -.5f, .9f, 1.4f, 3),
            Ease(Interp.LINEAR, 0f, 0f, 1f, 1f),
            Ease(Interp.BOUNCE, 0f, 0f, 1f, 1f),
            Ease(Interp.HOLD, 0f, 0f, 1f, 1f),
        )
        for (e in cases) {
            val v = curvePresetValues(e)
            assertEquals(6, v.size)
            val back = curvePresetEase(v)
            assertTrue("$e -> $back", back.same(e))
            for (t in listOf(.1f, .5f, .9f)) assertEquals(e.transform(t), back.transform(t), 1e-5f)
        }
        // A força só vale na bézier: nomeadas vão com ×1.
        assertEquals(1f, curvePresetValues(Ease(Interp.EASE_IN, 0f, 0f, 1f, 1f, 3))[5], 0f)
        assertEquals(2f, curvePresetValues(cases[0])[5], 0f)
    }
}
