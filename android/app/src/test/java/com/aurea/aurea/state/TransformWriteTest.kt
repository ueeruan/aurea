package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * O dedo na cena 3D e no palco escrevem pela MESMA regra: trilha animada com
 * Auto-Key marca keyframe no cabeçote — antes a cena forçava "deslocar a curva
 * inteira" e nenhum keyframe de nulo/objeto arrastado ali era marcado.
 */
class TransformWriteTest {
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
