package com.aurea.aurea.editor.panels

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Bug 6b (0.3.5): a roda de rotação "não respondia ao toque". O toque parado
 * só abria o teclado com menos de 100 ms (o `tapTimeout`), e um toque de dedo
 * dura 100–200 ms. Agora vale até o prazo do toque longo (500 ms no Android;
 * `TransformPadState.holdTime` no iOS).
 */
class RotationDialTapTest {
    private val longPress = 500L

    @Test fun ordinaryFingerTapsOpenTheKeypad() {
        for (ms in listOf(0L, 40L, 99L, 100L, 160L, 250L, 499L)) {
            assertTrue("$ms ms", isDialTap(ms, longPress))
        }
    }

    @Test fun holdingPastTheLongPressIsNotATap() {
        assertFalse(isDialTap(500L, longPress))
        assertFalse(isDialTap(900L, longPress))
        assertFalse(isDialTap(-1L, longPress))
    }
}
