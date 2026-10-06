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

    /**
     * Beta 2026-10-05: arrastar um texto com chaves de posição não fazia nada.
     * O gesto não decide mais pelo detalhe lido (que pode estar atrasado): manda
     * os bits do motor — só se mudou, no quadro da prévia, valor parado quando
     * a trilha não tem chave. Os números são os de Command.hpp (kAutoKey*).
     */
    @Test fun stageGestureLetsTheEngineKeyTheShownFrameOrSetTheStaticValue() {
        assertEquals(1, AUTO_KEY_ONLY_IF_CHANGED)
        assertEquals(2, AUTO_KEY_AT_PLAYHEAD)
        assertEquals(4, AUTO_KEY_STATIC_WHEN_UNANIMATED)
        assertEquals(7, gestureKeyFlags(wholeGroup = false))
        // O grupo XYZ do 3D animado: todo eixo ganha chave, nunca valor parado.
        assertEquals(3, gestureKeyFlags(wholeGroup = true))
    }

    @Test fun autoKeyOffAlwaysShiftsTheWholeAnimation() {
        for (scene in listOf(true, false)) for (animated in listOf(true, false)) {
            assertEquals("scene=$scene animated=$animated", TransformWrite.Layout, transformWrite(scene, autoKey = false, animated = animated))
        }
    }
}
