package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Assert.assertArrayEquals
import org.junit.Test

/**
 * O dedo na cena 3D e no palco escrevem pela MESMA regra: trilha animada com
 * Auto-Key marca keyframe no cabeçote — antes a cena forçava "deslocar a curva
 * inteira" e nenhum keyframe de nulo/objeto arrastado ali era marcado.
 */
class TransformWriteTest {
    @Test fun linkedScalePreservesAllThreeRatiosFromGestureStart() {
        val start = floatArrayOf(1f, 2f, 3f)
        for (axis in 0..2) {
            assertArrayEquals(floatArrayOf(2f, 4f, 6f), linkedScale(start, axis, start[axis] * 2), .0001f)
            assertArrayEquals(floatArrayOf(-1f, -2f, -3f), linkedScale(start, axis, -start[axis]), .0001f)
        }
        assertArrayEquals(floatArrayOf(2f, 2f, 2f), linkedScale(floatArrayOf(0f, 0f, 0f), 2, 2f), .0001f)
    }

    @Test fun editingAnyAxisKeysTheEntireAnimated3DVector() {
        for (base in listOf(0, 3, 6, 9)) for (animatedAxis in 0..2) for (editedAxis in 0..2) {
            assertArrayEquals(intArrayOf(base, base + 1, base + 2),
                transformKeyGroup(base + editedAxis, true, 1 shl (base + animatedAxis)))
        }
    }
    @Test fun groupedKeysNeverSpillIntoOtherPropertiesOrPlanarLayers() {
        assertArrayEquals(intArrayOf(), transformKeyGroup(0, true, 1 shl 6))
        assertArrayEquals(intArrayOf(), transformKeyGroup(0, false, 7))
        assertArrayEquals(intArrayOf(), transformKeyGroup(12, true, 1 shl 12))
        assertArrayEquals(intArrayOf(), transformKeyGroup(0, true, 0))
    }

    @Test fun animatedTrackInTheSceneKeysThePlayhead() {
        assertEquals(TransformWrite.Keyframe, transformWrite(sceneEditor = true, autoKey = true, animated = true))
        assertEquals(TransformWrite.Keyframe, transformWrite(sceneEditor = false, autoKey = true, animated = true))
    }

    @Test fun staticTrackLaysOutInTheSceneAndSetsTheValueInTheTimeline() {
        assertEquals(TransformWrite.Layout, transformWrite(sceneEditor = true, autoKey = true, animated = false))
        assertEquals(TransformWrite.Static, transformWrite(sceneEditor = false, autoKey = true, animated = false))
    }

    @Test fun autoKeyOffAlwaysShiftsTheWholeAnimation() {
        for (scene in listOf(true, false)) for (animated in listOf(true, false)) {
            assertEquals("scene=$scene animated=$animated", TransformWrite.Layout, transformWrite(scene, autoKey = false, animated = animated))
        }
    }
}
