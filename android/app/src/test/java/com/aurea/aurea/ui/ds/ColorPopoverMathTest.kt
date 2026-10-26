package com.aurea.aurea.ui.ds

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Omega aparência (área 05, P0-5): onde o balão do seletor abre e as contas
 * HSV do seletor embutido. Mesmos casos do `ColorPopoverMath` do iOS.
 */
class ColorPopoverMathTest {
    // Janela 400 × 800; balão 290 × 146; precisa de 153 abaixo; margem 8; seta a 30; meia seta 9; raio 5.
    private fun place(left: Int, top: Int, right: Int, bottom: Int) =
        ColorPopoverMath.place(left, top, right, bottom, 400, 800, 290, 146, need = 153, margin = 8, arrowInset = 30, arrowHalf = 9, radius = 5)

    @Test fun opensBelowWhenItFits() {
        val p = place(20, 100, 44, 124)
        assertTrue(p.below)
        assertEquals(124, p.y)
    }

    @Test fun opensAboveNearTheBottom() {
        val p = place(20, 700, 44, 724)
        assertFalse(p.below)
        assertEquals(700 - 146, p.y)
    }

    @Test fun arrowSideFollowsTheHalfOfTheScreen() {
        // Âncora à esquerda: balão começa a 30 do centro dela (preso na margem).
        val left = place(20, 100, 44, 124)
        assertEquals(8, left.x)
        assertEquals(32 - 8, left.arrowX)
        // Âncora à direita: o balão termina a 30 do centro dela.
        val right = place(340, 100, 364, 124)
        assertEquals(352 - (290 - 30), right.x)
        assertEquals(290 - 30, right.arrowX)
    }

    @Test fun staysInsideTheWindow() {
        val p = place(380, 100, 400, 124)
        assertEquals(400 - 290 - 8, p.x)
        assertTrue(p.arrowX <= 290 - 5 - 9)
    }

    @Test fun hsvRoundTrip() {
        val hsv = FloatArray(3)
        rgbToHsv(1f, 0.5f, 0f, hsv)
        assertEquals(30f, hsv[0], 0.01f)
        assertEquals(1f, hsv[1], 0.001f)
        assertEquals(1f, hsv[2], 0.001f)
        val rgb = FloatArray(3)
        hsvToRgb(hsv[0], hsv[1], hsv[2], rgb)
        assertEquals(1f, rgb[0], 0.001f)
        assertEquals(0.5f, rgb[1], 0.001f)
        assertEquals(0f, rgb[2], 0.001f)
    }

    @Test fun argbRoundTrip() {
        val argb = rgbaToArgb(floatArrayOf(1f, 0f, 0.5f, 0.5f))
        val back = argbToRgba(argb)
        assertTrue(sameColor(floatArrayOf(1f, 0f, 0.5f, 0.5f), back, 1f / 255f))
    }
}
